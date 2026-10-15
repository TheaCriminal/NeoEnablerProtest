"""Offline case integrity and scanner tests; no console or network access."""
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("firmware_report", Path(__file__).resolve().parents[1] / "tools/firmware_report.py")
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)


def fixture(firmware="0x13400000"):
    rows = [
        {"format": tool.FORMAT, "record": "session", "firmware_raw": firmware,
         "firmware_rc": 0, "firmware_bytes": 4, "sdk_raw": "0x13200006", "scope": "metadata_only",
         "patches": False, "snapshot_atomic": False, "test_fixture": True},
        {"format": tool.FORMAT, "record": "process", "pid": 123, "kind": "game", "title_id": "CUSA00093", "mode": "neo"},
        {"format": tool.FORMAT, "record": "query", "pid": 123, "node": "kern.app.dmem_size_pid",
         "rc": 0, "returned_bytes": 8, "value_hex": "0x0000000150000000"},
        {"format": tool.FORMAT, "record": "end", "records_before_end": 3,
         "scan_complete": True, "game_count": 1, "scan_rc": 0},
    ]
    return rows


class FirmwareReportTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.diagnostic = self.root / "fixture.jsonl"
        self.write(fixture())

    def tearDown(self):
        self.temp.cleanup()

    def write(self, rows):
        self.diagnostic.write_text("\n".join(json.dumps(r) for r in rows) + "\n")

    def test_new_firmware_never_enabled_by_metadata(self):
        report = tool.make_case(self.diagnostic, self.root / "case")
        self.assertEqual(report["firmware_raw"], "0x13400000")
        self.assertIsNone(report["reference_profile"])
        self.assertFalse(report["automatic_patch_allowed"])
        self.assertIsNone(report["kernel"])
        self.assertEqual(report["assessment"], "unvalidated_case")

    def test_actual_firmware_not_sdk(self):
        _, rows = tool.load_diagnostic(self.diagnostic)
        self.assertNotEqual(rows[0]["firmware_raw"], rows[0]["sdk_raw"])
        rows[0]["firmware_rc"] = -1
        self.write(rows)
        with self.assertRaisesRegex(ValueError, "Actual firmware"):
            tool.make_case(self.diagnostic, self.root / "case")

    def test_truncated_concatenated_corrupt_sessions(self):
        original = fixture()
        for rows in [original[:-1], original + original, original[:1] + original[2:]]:
            with self.subTest(rows=len(rows)):
                self.write(rows)
                with self.assertRaises(ValueError):
                    tool.load_diagnostic(self.diagnostic)
        self.diagnostic.write_text("loader noise\n")
        with self.assertRaises(ValueError):
            tool.load_diagnostic(self.diagnostic)

    def test_partial_scan_and_inconsistent_query_refused(self):
        rows = fixture();rows[-1]["scan_complete"] = False;self.write(rows)
        with self.assertRaises(ValueError):
            tool.load_diagnostic(self.diagnostic)
        rows = fixture();rows[2]["rc"] = -1;self.write(rows)
        with self.assertRaises(ValueError):
            tool.load_diagnostic(self.diagnostic)

    def test_unknown_schema_and_game_count(self):
        rows = fixture();rows[0]["format"] = "future";self.write(rows)
        with self.assertRaises(ValueError):
            tool.load_diagnostic(self.diagnostic)
        rows = fixture();rows[-1]["game_count"] = 0;self.write(rows)
        with self.assertRaises(ValueError):
            tool.load_diagnostic(self.diagnostic)

    def test_arithmetic_and_branch_only_candidates(self):
        blob = bytearray(512)
        blob[0:10] = tool.BASELINE
        branch, target = 0x32, 0x180
        blob[branch:branch + 12] = b"\x49\x81\xc7" + struct.pack("<i", 0x28000000) + b"\xe9" + struct.pack("<i", target - branch - 12)
        findings = tool.scan_text(bytes(blob))
        self.assertEqual(findings["arithmetic_candidates"][0]["arithmetic_result_bytes"], 0x150000000)
        self.assertEqual(findings["arithmetic_candidates"][0]["jump_destination"], "0x180")
        self.assertFalse(findings["automatic_patch_allowed"])
        # A branch leaving the supplied TEXT cannot be called a valid destination.
        blob[branch + 8:branch + 12] = struct.pack("<i", -1000)
        self.assertIsNone(tool.scan_text(bytes(blob))["arithmetic_candidates"][0]["jump_destination"])

    def test_partial_patterns_elf_and_limits(self):
        self.assertEqual(tool.scan_text(tool.BASELINE)["arithmetic_candidates"], [])
        with self.assertRaises(ValueError):
            tool.scan_text(b"\x7fELF" + bytes(128))
        hits, truncated = tool.positions(b"a" * 200, b"a", 128)
        self.assertEqual(len(hits), 128);self.assertTrue(truncated)

    def test_case_hashes_and_evidence_never_overwritten(self):
        kernel = self.root / "raw.bin";kernel.write_bytes(bytes(512))
        report = tool.make_case(self.diagnostic, self.root / "case", kernel)
        self.assertEqual(report["kernel"]["sha256"], tool.hashlib.sha256(kernel.read_bytes()).hexdigest())
        self.assertEqual((self.root / "case/kernel-text.bin").read_bytes(), kernel.read_bytes())
        with self.assertRaisesRegex(ValueError, "exists"):
            tool.make_case(self.diagnostic, self.root / "case")
        with self.assertRaises(ValueError):
            tool.make_case(self.diagnostic, self.root / "bad", kernel, (1 << 64) - 1)

    def test_known_reference_is_still_unvalidated_case(self):
        self.write(fixture("0x13200000"))
        report = tool.make_case(self.diagnostic, self.root / "case")
        self.assertIsNotNone(report["reference_profile"])
        self.assertFalse(report["automatic_patch_allowed"])
        self.assertTrue(report["hardware_validation_required"])


if __name__ == "__main__":
    unittest.main()
