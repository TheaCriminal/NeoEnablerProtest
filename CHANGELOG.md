# Changelog

## Unreleased

- The payload file is now named `neo-manager.elf`.
- The game patch files only carry patches made in this project. Community patches (60 FPS, debug camera and
  others) were removed and stay available from their original authors.
- Bloodborne patches renamed to the same pattern as the other games ("1440p (PS4 Pro mode)" and so on). If you
  had one enabled, enable it again after updating the file.
- Patch descriptions in Bloodborne, Just Cause 3 and Resident Evil 6 rewritten in plain language.
- The Order: 1886, Native 4K: fixed faint vertical stripes on characters under strong light.
- The Order: 1886, Native 4K: on-screen text, objectives and button prompts are no longer cut in the middle of the
  screen.
- The Order: 1886, Native 4K: fixed small black and grey blocks flickering on trees, bushes and other foliage, also
  visible as dark outlines on clothing edges.
- New game: Driveclub (01.28), 1080p in PS4 Pro mode.
- README: explains how to add the patches to a patch file the game already has, and that every game needs its
  patch to work in PS4 Pro mode.

## 0.1.1

- The control panel shows whether a game runs in PS4 Pro mode in the same way as before on firmwares where that
  already worked. The new check from 0.1.0 is only used for games where the old one finds nothing.
- New README written as a user guide. Technical details moved to `docs/TECHNICAL.md`.

## 0.1.0

First public release.

- Makes PS4 games without PS4 Pro support start in PS4 Pro mode. Games with Pro support are not changed.
- Control panel at `http://<PS5 IP>:8765` and a "NEO Enabler" shortcut in the Media tab: turn PS4 Pro mode on or
  off and see the open PS4 game, its name and whether it runs in Pro mode.
- Fixed: the panel showed "No PS4 game running" on games that use the PS4 compatibility graphics driver.
- Turning it off also removes the hook of older NEO Enabler builds (v4).
- Game patches in `xml/` for Need for Speed Rivals, Resident Evil 6, Bloodborne, Just Cause 3, Battlefield 4,
  Killzone Shadow Fall and The Order: 1886.
- Firmwares 4.00 to 13.60 (tested on 13.20). Needs ps-patch-system loaded first.
