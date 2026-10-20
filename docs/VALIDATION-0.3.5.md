# 0.3.5 validation

The final release promotes the padding-test implementation with the build name
`NeoEnabler 0.3.5`. The hook, memory transaction and firmware profiles are unchanged
from that candidate.

## Console evidence

- 13.20: Bloodborne gameplay with the reported 8192 MiB budget, expected constructor
  value, and automatic restoration after closing the game were verified in the
  [experimental regression](VALIDATION-0.3.5-experimental.md). The maintainer also
  confirmed Native 4K gameplay.
- 13.60: an earlier community report confirmed Bloodborne Native 4K gameplay with
  8 GiB enabled in the panel. The affected console subsequently refused hook
  installation because the prefix before the reservation pool was occupied.
  After the [padding fix](PADDING-PREFLIGHT-1360.md), the user reported that it
  worked and the game opened successfully. Post-close restoration status has not
  been supplied for that console.

These reports do not establish every game's compatibility, long-term stability,
or full-range memory backing. The final version-label build has not separately
been run on a console. The earlier cached firmware-reader experiment was withdrawn;
the final build retains the original firmware query.

## Release checks

The SDK build uses `-Wall -Wextra -Werror`. Host checks cover the memory transaction,
hook lifecycle, rollback, controller ownership, native shim ABI and synchronization,
GPU policy, patch synchronization, panel behavior and the diagnostic toolkit.
The padding regression preserves the reported prefix bytes, while foreign bytes
inside any actual reservation slot still prevent installation.

The final SDK build and all listed host checks passed before publication, including
GCC and Clang ASan/UBSan checks for the DATA transaction, hook and controller.
Local release ELF SHA256:
`65b7c27645fa54f2f7d5028236f7f413c08b0e28ce522011e3e3d6f268b30116`.
