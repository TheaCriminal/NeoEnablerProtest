# Experimental no-HV 8 GiB on 13.20 and 13.60

Branch: `release/v0.3.5-experimental`. The manager integrates the existing
13.20 DATA transaction and a firmware-specific launch profile for 13.60.
13.20 has a Bloodborne gameplay/constructor/restoration regression test;
13.60 has offline validation only and needs console reports.

## Transaction and synchronization

The DATA transaction derives from the immutable reference
`9448223a50594ea192faf065a15ce7b16a907ed0`. It requires a successful PS4 mount,
POST/CLAIM/HELD confirmation with the same sequence and an eligible, stable
container. It checks actual Pro mode, category, flags, app identity, references,
profile, stock globals, backing and available capacity before changing the
extension. The extension is an increment: only byte+3 changes, zero to `0xc0`,
for the guarded 3 GiB increment. No reported area-size field, kernel TEXT or
page-table entry is written. This is a fixed firmware table, not generic discovery.

The gate remains held until game teardown, so another intercepted mount cannot
consume the global increment. The constructor must report `0x24c000000` for
field38. Teardown requires the container and references to be absent, no running
game and no active shim callback. Rollback restores the full original global
before dirty and gate are cleared. A failed rollback, uncertain acknowledgment,
changed owner or constructor deadline blocks further mutation and requires
recovery/reboot; a timeout alone never authorizes releasing a live callback.

Userland hook installation validates the exact function prologue, call bytes,
import stub, GOT target and libkernel wrapper. It confirms SIGSTOP through
bounded KERN_PROC_PROC enumeration, rechecks under suspension and reads back
writes. A partial call write must be restored before resuming. Code reservations
remain after detach and are never reused, because another thread may still
return through retained code. Existing userland pages must already be RWX;
NeoEnabler does not change kernel protections to obtain access.

## DATA offsets

| Field | 13.20 DATA offset | 13.60 DATA offset |
| --- | --- | --- |
| Extension | `0x2ee00b8` | `0x2ef40b8` |
| Container pointer | `0x2ed9fe0` | `0x2eedfe0` |
| Backing base | `0x2ed9fb8` | `0x2eedfb8` |
| References | `0x2ee0110` | `0x2ef4110` |
| Profile | `0x2ee1400` | `0x2ef5400` |
| Maximum extension | `0x2ee1418` | `0x2ef5418` |

Kernel evidence includes the 13.60 extension accessor load at TEXT+`0xa03d6b`
and constructor read at TEXT+`0xa016ac`. Both reference DATA+`0x2ef40b8` under
the captured TEXT-to-DATA delta `0xcc0000`. Other guarded fields retain the
reference layout; this does not establish a fully authenticated container ABI
or backing validation on 13.60 hardware.

## Userland profiles

| Field | 13.20 module RVA | 13.60 module RVA |
| --- | --- | --- |
| BeginAppMount2 caller | `0x6d6dc0` | `0x6d7af0` |
| Intercepted CALL | `0x6d7c11` | `0x6d8941` |
| Import stub | `0x195b690` | `0x195c3b0` |
| GOT slot | `0x2067458` | `0x2067460` |
| libkernel_sys wrapper | `0x42850` | `0x42850` |
| Fresh reservations start | `0x1962000` | `0x1963000` |
| Padding end | `0x1964000` | `0x1964000` |

The 13.60 GOT has the unique JUMP_SLOT relocation for NID `HRDq-RDIGmI`;
libkernel exports the corresponding wrapper at `0x42850`. The wrapper's first
48 bytes match 13.20, while six relative operand bytes differ in the captured
body. Padding is in the executable mapping's page-rounded tail and is checked
for zero bytes and RWX at runtime.

[13.60-INPUT-VALIDATION.json](13.60-INPUT-VALIDATION.json) records current ELF
hashes and earlier SELF metadata. Both ELFs have their file-backed segments
within bounds and readable instructions. Their whole-file hashes differ from
the original SELF's expected ELF digests; reconstruction/dumper changes are
possible but not established. Firmware provenance is supplied by the maintainer
and the exact runtime version is required. No independent authenticity claim.

## Tests and limits

GCC and Clang host tests, including ASan/UBSan, cover transaction guards and
rollback, exact hook profiles, stop/write/readback failures, retained reservations,
detach races and controller ownership/recovery. Native execution of the shim
checks stack alignment, callee-saved registers, all three arguments, 64-bit return,
POST expiration, CLAIM/HELD, stale commands/sequences and gate contention.
These tests do not replace a 13.60 console test or full allocation/backing tests.

See [VALIDATION-0.3.5-experimental.md](VALIDATION-0.3.5-experimental.md) for the
13.20 session. Use a fresh boot and do not mix an older TEXT memory patch with
this DATA experiment. Keep the manager running until the game closes and its
owned extension is restored. Quit refuses an active or unresolved transaction.
