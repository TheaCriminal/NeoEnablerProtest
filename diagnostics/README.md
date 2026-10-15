# Firmware diagnostic toolkit

Collects metadata from other PS5 firmware versions so the 8 GiB budget can be ported.
Does not apply patches, change settings or unlock anything on the console.

## Firmware status

| Firmware | NEO (Pro mode) | 8 GiB budget | Text-to-data delta | What we need |
| --- | --- | --- | --- | --- |
| 4.00 - 13.00 | Supported | Not started | Unknown | Diagnostic capture + kernel TEXT dump |
| 13.20 | Supported | Validated | `0x00CB0000` | Done |
| 13.60 | Supported | Needs TEXT dump | `0x00CC0000` | Kernel TEXT dump to locate the budget constructor |
| 14.00+ | No jailbreak | Not possible | N/A | N/A |

## What it contains

- `neo-diagnostic.elf`: portable metadata collector. Prints JSONL to the sender's stdout.
- `tools/firmware_report.py`: offline analysis tool (Python 3 standard library only).
- `compatibility.json`: research registry. Only 13.20 is validated; absent versions default to unsupported. Not loaded by the manager.

The diagnostic uses no firmware-specific offsets and does not read kernel TEXT. It needs a jailbreak and SDK loader that support the firmware, but not a hypervisor unlock.

## Collecting data

1. Note your firmware version, console model, jailbreak method and whether the hypervisor is unlocked.
2. Build and send **only the diagnostic**, never an adapted 13.20 patch. To capture a game's budget, leave it on a stable screen. Collect once without a game and once with a game open, and note whether the game has native Pro support and which patches are active.
3. Save the complete stdout:

```sh
bash diagnostics/build.sh
/opt/ps5-payload-sdk/bin/prospero-deploy -h PS5_IP build/neo-diagnostic.elf > diagnostic.jsonl 2> sender.log
```

Do not remove lines from the JSONL. It must start with `session` and end with `end` containing `scan_complete:true`. If the sender mixes its own logs into stdout, keep the original and separate the diagnostic lines into a second file.

## What to submit

- `diagnostic.jsonl` (required).
- A raw kernel TEXT dump starting at offset 0, captured separately by a trusted tool after hypervisor unlock (optional but needed to locate the budget constructor).
- Firmware version, console model, jailbreak method, hypervisor unlock method, and the tool used for the TEXT dump.

Do not include kernel dumps in a public PR. Transfer them separately.

## Offline analysis

These commands need no console or network:

```sh
python3 tools/firmware_report.py --diagnostic diagnostic.jsonl --out cases/fw-report
python3 tools/firmware_report.py --diagnostic diagnostic.jsonl --kernel-text kernel-text.bin --out cases/fw-with-text
```

`--out` must be a new directory; existing evidence is never overwritten. `--text-base 0x...` is optional and must come from an independent measurement. The output contains `diagnostic.jsonl`, `report.json` and the kernel TEXT if provided. The scanner reports only **candidates**. A match does not prove the code belongs to the budget path, and no match does not prove the path is absent. The report always sets `automatic_patch_allowed:false`.

## Criteria for adding firmware support

1. Verify dump origin, integrity and actual firmware version.
2. Disassemble candidates and trace the constructor, budget, backing, neighboring reserves and capacity checks.
3. Identify DATA/pmap layout and access preconditions for that firmware.
4. Prepare complete guards, rollback and offline tests for the real flow.
5. Test on a console running that firmware: budget, allocation, access to the additional range, reserves, gameplay and rollback. Confirm refusal when access is unavailable.
6. Only then update the whitelist. An offline result never promotes a version to supported.

## Testing this toolkit

```sh
python3 -m unittest discover -s tests -p 'test_firmware_report.py' -v
```

Built with SDK `-Werror`. The diagnostic has not been sent to a console yet; behavior on other firmwares has not been tested. GPL-3.0.
