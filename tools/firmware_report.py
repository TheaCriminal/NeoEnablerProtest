#!/usr/bin/env python3
"""Package PS5 metadata and optionally scan a supplied raw kernel TEXT offline.

No console connection, code execution, patch generation or compatibility enable.
Uses the Python standard library only. All offsets are relative to the supplied
raw TEXT; matching arithmetic is a research candidate, never a firmware port.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import sys

FORMAT = "neo-diagnostic-v1"
MAX_METADATA = 16 * 1024 * 1024
MAX_TEXT = 128 * 1024 * 1024
BASELINE = bytes.fromhex("49bf0000002801000000")
ORIGINAL = bytes.fromhex("4981c700000028e934020000")
EIGHT = bytes.fromhex("49bf0000000002000000eb36")


def load_diagnostic(path):
    path = Path(path)
    if not 0 < path.stat().st_size <= MAX_METADATA:
        raise ValueError("Diagnostic must be nonempty and at most16MiB")
    raw = path.read_bytes()
    try:
        rows = [json.loads(line) for line in raw.decode("utf-8").splitlines() if line.strip()]
    except (UnicodeError, json.JSONDecodeError) as exc:
        raise ValueError("Diagnostic must contain complete JSON lines; keep loader messages in another file") from exc
    if len(rows) < 2 or any(not isinstance(r, dict) or r.get("format") != FORMAT for r in rows):
        raise ValueError("Unknown diagnostic format")
    first, end = rows[0], rows[-1]
    if first.get("record") != "session" or end.get("record") != "end":
        raise ValueError("Missing session header or completion footer (possibly truncated capture)")
    if any(r.get("record") in {"session", "end"} for r in rows[1:-1]):
        raise ValueError("Multiple or concatenated sessions")
    if end.get("scan_complete") is not True or end.get("scan_rc") != 0:
        raise ValueError("Process collection did not complete")
    if end.get("records_before_end") != len(rows) - 1:
        raise ValueError("Record count mismatch (possibly truncated capture)")
    if first.get("firmware_rc") != 0 or first.get("firmware_bytes") != 4:
        raise ValueError("Actual firmware query failed; SDK version must not be substituted")
    if not re.fullmatch(r"0x[0-9a-fA-F]{8}", first.get("firmware_raw", "")):
        raise ValueError("Invalid firmware identifier")
    if first.get("scope") != "metadata_only" or first.get("patches") is not False:
        raise ValueError("Unexpected collection scope")
    processes = [r for r in rows if r.get("record") == "process"]
    games = [r for r in processes if r.get("kind") == "game"]
    if len(games) != end.get("game_count"):
        raise ValueError("Game count mismatch")
    for row in rows[1:-1]:
        if row.get("record") not in {"process", "query"}:
            raise ValueError("Unknown record type")
        if type(row.get("pid")) is not int or row["pid"] <= 0:
            raise ValueError("Invalid process ID")
        if row["record"] == "query":
            value = row.get("value_hex")
            if value is not None and not re.fullmatch(r"0x[0-9a-fA-F]{16}", str(value)):
                raise ValueError("Invalid query value")
            if value is not None and (row.get("rc") != 0 or row.get("returned_bytes") != 8):
                raise ValueError("Query value supplied despite an error or unexpected size")
    return raw, rows


def positions(blob, pattern, limit=128):
    found, start = [], 0
    while len(found) < limit:
        off = blob.find(pattern, start)
        if off < 0:
            return found, False
        found.append(off)
        start = off + 1
    return found, blob.find(pattern, start) >= 0


def offset(n):
    return f"0x{n:x}"


def scan_text(blob):
    if not 0 < len(blob) <= MAX_TEXT:
        raise ValueError("Raw kernel TEXT must be nonempty and at most128MiB")
    if blob.startswith(b"\x7fELF"):
        raise ValueError("Supply extracted raw TEXT starting at offset0, not an ELF container")
    bases, base_truncated = positions(blob, BASELINE)
    candidates = []
    for base in bases:
        # Bounded window around the reference NEO arithmetic, no disassembler
        # dependency and no assumption that a match proves an instruction boundary.
        for at in range(base + len(BASELINE), min(base + 0x100, len(blob) - 11)):
            if blob[at:at + 3] != b"\x49\x81\xc7" or blob[at + 7] != 0xE9:
                continue
            addition = struct.unpack_from("<i", blob, at + 3)[0]
            destination = at + 12 + struct.unpack_from("<i", blob, at + 8)[0]
            candidates.append({
                "kind": "reference_like_budget_arithmetic",
                "load_offset": offset(base), "branch_offset": offset(at),
                "add_immediate_signed": addition,
                "arithmetic_result_bytes": (0x128000000 + addition) & ((1 << 64) - 1),
                "jump_destination": offset(destination) if 0 <= destination < len(blob) else None,
                "bytes": blob[at:at + 12].hex(),
                "warning": "Byte-pattern candidate only; branch ownership, execution path and allocations unproven",
            })
    known, known_truncated = positions(blob, ORIGINAL)
    patched, patched_truncated = positions(blob, EIGHT)
    constants = {}
    for name, value in [("4.5GiB", 0x120000000), ("5.25GiB", 0x150000000), ("5.5GiB", 0x160000000), ("8GiB", 0x200000000)]:
        hits, truncated = positions(blob, struct.pack("<Q", value), 32)
        constants[name] = {"offsets": [offset(x) for x in hits], "truncated": truncated}
    return {
        "baseline_offsets": [offset(x) for x in bases], "baseline_list_truncated": base_truncated,
        "arithmetic_candidates": candidates,
        "exact_13_20_original_sequence_offsets": [offset(x) for x in known],
        "exact_13_20_8gib_sequence_offsets": [offset(x) for x in patched],
        "exact_sequence_lists_truncated": known_truncated or patched_truncated,
        "constant_occurrences_not_proven_limits": constants,
        "automatic_patch_allowed": False,
    }


def make_case(diagnostic, out, kernel_text=None, text_base=None, registry=None):
    raw, rows = load_diagnostic(diagnostic)
    session = rows[0]
    out = Path(out)
    if out.exists():
        raise ValueError("Output directory exists; refusing to overwrite evidence")
    kernel = None
    blob = None
    if kernel_text:
        source = Path(kernel_text)
        if not 0 < source.stat().st_size <= MAX_TEXT:
            raise ValueError("Raw kernel TEXT must be nonempty and at most128MiB")
        blob = source.read_bytes()
        findings = scan_text(blob)
        if text_base is not None and not (0xffff800000000000 <= text_base < (1 << 64) - len(blob)):
            raise ValueError("TEXT base must be a canonical kernel address with no range overflow")
        kernel = {"file": "kernel-text.bin", "sha256": hashlib.sha256(blob).hexdigest(),
                  "size": len(blob), "text_base": offset(text_base) if text_base is not None else None,
                  "base_source": "user_supplied_unverified" if text_base is not None else "not_supplied",
                  "findings": findings}
    elif text_base is not None:
        raise ValueError("--text-base requires --kernel-text")
    profile = None
    if registry is None:
        registry = Path(__file__).resolve().parents[1] / "diagnostics/compatibility.json"
    registry_data = json.loads(Path(registry).read_text())
    if registry_data.get("format") != "neo-firmware-registry-v1":
        raise ValueError("Unknown research registry format")
    for item in registry_data["profiles"]:
        if item["firmware_raw"].lower() == session["firmware_raw"].lower():
            profile = item
            break
    report = {
        "format": "neo-firmware-case-v1", "firmware_raw": session["firmware_raw"].lower(),
        "diagnostic": {"file": "diagnostic.jsonl", "sha256": hashlib.sha256(raw).hexdigest(),
                       "records": len(rows), "session": session,
                       "processes_and_queries": rows[1:-1]},
        "kernel": kernel,
        "reference_profile": profile,
        "reference_hash_matches": bool(kernel and profile and kernel["sha256"] == profile.get("reference_kernel_text_sha256")),
        "assessment": "unvalidated_case", "automatic_patch_allowed": False,
        "hardware_validation_required": True,
        "limitations": ["Metadata and snapshots are not atomic", "No kernel captured by the diagnostic payload",
                        "Byte matches are candidates, not validated code paths or safe patch offsets",
                        "This tool never enables firmware support or generates a payload patch"],
    }
    out.mkdir(parents=True)
    (out / "diagnostic.jsonl").write_bytes(raw)
    if blob is not None:
        (out / "kernel-text.bin").write_bytes(blob)
    (out / "report.json").write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--diagnostic", type=Path, required=True, help="Complete stdout from neo-diagnostic.elf")
    parser.add_argument("--out", type=Path, required=True, help="New evidence directory (must not exist)")
    parser.add_argument("--kernel-text", type=Path, help="Optional raw TEXT captured separately by a trusted tool")
    parser.add_argument("--text-base", type=lambda x: int(x, 0), help="Optional independently verified TEXT address")
    args = parser.parse_args(argv)
    try:
        report = make_case(args.diagnostic, args.out, args.kernel_text, args.text_base)
    except (OSError, ValueError, KeyError, TypeError) as exc:
        parser.exit(2, f"Refused: {exc}\n")
    print(f"Saved {args.out / 'report.json'}; firmware={report['firmware_raw']}; automatic_patch_allowed=false")
    return 0


if __name__ == "__main__":
    sys.exit(main())
