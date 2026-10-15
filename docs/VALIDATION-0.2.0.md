# Local validation of version 0.2.0 — 2026-10-15

Status: built locally, not yet sent to the PS5. No commit, tag, push or remote release at the time of writing. Artifacts in `build/neo-manager.elf` and `build/neo-manager-0.2.0.elf`.

SHA-256 of both:
`1250942defff7e46e70068a60582c1183fc98b254071d0d113ae79e0bbdfb598`.

Results:

- SDK prospero-clang, `-O2 -Wall -Wextra -Werror`: passed.
- Host tests `tests/budget_test.c`: passed. No console access.
- x86 verifier using the real 13.20 TEXT capture: original 5.25 GiB, step 5.5 GiB and 8 GiB all reach the original continuation.
- Version in manager/CHANGELOG and release-notes extraction: passed.
- Budget module contains no Byepervisor cookie or marker offset.
- Checksums of both saved artifacts verified; diff has no whitespace errors.
- CI configured to run host tests; not triggered remotely yet.

Tests cover 4 KiB/2 MiB/1 GiB pages, present/RW/XOTEXT permissions, read failures without accessing instructions when permissions refuse, unknown firmware/layout, no games/active areas, state change between reads, write/readback failures, rollback and blocking after unconfirmed rollback. Also cover automatic enabling after access becomes available, waiting for game close, manual disable without re-enable and later manual enable.

The previous 8 GiB selection was measured on Driveclub and the user confirmed normal gameplay with a memory-using patch. This does not validate the new detector: there was no real 0.2.0 test with the hypervisor protected, unlocked or with different methods. Guest PTEs are a precondition, not an attestation of global NPT/GMET/HV state. See `TECHNICAL.md` for the mechanism and limits.

Next deployment requires explicit authorization. Quit the experimental panel on port 8766 before using 0.2.0; Quit keeps patches. Version 0.2.0 uses port 8765 and enables NEO and tries 8 GiB on startup. Budget changes wait for all games to close.
