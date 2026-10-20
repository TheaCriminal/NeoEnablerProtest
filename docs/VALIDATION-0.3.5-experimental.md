# 0.3.5-experimental validation

Date: 2026-10-20. Branch: `release/v0.3.5-experimental`.
Tested ELF SHA256: `1f517c869d7b98e9c4af5321e01ffd43171f68f06d5c89968893cf8794bf12a8`.

## Firmware 13.20 hardware regression

The existing manager reported kernel code access unavailable before the test.
The first experimental build refused hook installation because its KERN_PROC_PID
query did not return the expected full process record. No DATA transaction was
made by that build. The adapter was changed to the bounded KERN_PROC_PROC
enumeration used by the historical observer, host tests reran, and the corrected
build was sent after stopping the previous manager through its HTTP endpoint.

The corrected build installed the no-HV launch hook with rc=0. Bloodborne
CUSA00900 v01.09 retained its existing 1440p Pro patch and the temporary force-8GiB
option exercised the DATA transaction. The observed successful launch recorded:

- HELD confirmed, apply rc=0, transaction owned.
- Constructor field38 `0x24c000000`, exact expected value.
- Runtime mode PS4 Pro and `kern.app.dmem_size_pid` reported 8192 MiB.
- The maintainer confirmed normal gameplay without error, then also confirmed
  gameplay with the native 4K patch. The 4K outcome is a user report, not a
  separately captured resolution measurement.
- Closing Bloodborne restored the original extension: owned=0, restored=1,
  gate=0, dirty=0, blocked=0. Temporary force-8GiB was then disabled again.

The session log also contains an earlier unsuccessful launch with a game SIGSEGV
at read address `0x44`, followed by a verified DATA rollback and a subsequent
successful launch. Its cause is unresolved; the successful gameplay report does
not establish that every launch or game is stable. No full-range CPU/GPU backing
or aliasing test was performed in this regression.

Local raw evidence is retained in `build/console-1320-regression/run2/`:
`send.json`, startup/gameplay/after-close/final status files and NeoEnabler logs.
Those raw files and Sony binaries are not release assets.

## Firmware 13.60

Implemented and validated offline against the supplied ELF and kernel captures.
One community report now confirms Bloodborne Native 4K gameplay with 8 GiB enabled in the panel.
Raw logs, a full backing test and post-close restoration verification were not supplied for that report. See [8GB-NO-HV-1360.md](8GB-NO-HV-1360.md)
and [13.60-INPUT-VALIDATION.json](13.60-INPUT-VALIDATION.json).

## Local checks

SDK payload build passes with `-Wall -Wextra -Werror`. GCC and Clang host tests
pass for the existing TEXT guards, DATA transaction, production hook lifecycle
and controller sequencing/recovery, including ASan/UBSan. Native shim ABI,
barrier, expiration, stale commands and contention tests pass, including a build
with ordinary host tools when the PS5 SDK is absent. Existing project tests are
also run before publication.

## Updated build: idle label and firmware identity

Updated ELF SHA256: `b77bcd4a70bb0895906d781cefe42ec0c6426bc493fd54d4888c22fe2e1d0ccd`.
The maintainer tested this exact binary on 13.20 and confirmed the panel showed
`ready` without a running game. The hook installed with rc=0, gate/dirty/owned
were zero, and recovery was not blocked. Evidence is retained locally under
`build/label-fix/console-test/`.

The actual firmware reader now caches only a complete successful sysctl result;
a later failed read cannot replace a recognized version with zero. GCC and Clang
ASan/UBSan regression tests cover initial failures, short responses, recovery
and stable identity across idle/gameplay. The panel distinguishes `ready`,
`enabled`, `unavailable`, `firmware unavailable` and `not supported`.
The memory transaction, firmware-specific offsets and mount hook are unchanged.
