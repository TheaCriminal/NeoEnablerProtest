# Changelog

## 0.2.1

- The manager writes every action and its result to the system log: startup, loaded settings, NEO on/off, 8 GiB
  requests and changes, Save Settings, errors and Quit, each followed by a `state:` line with the NEO hook and the
  8 GiB state. Lines start with `[NeoEnabler]` and are included when you use Save Logs (`/data/neo-logs.txt`).
- 8 GiB can only be turned on from firmware 13.20 (tested) or 13.60 (pending testing). On other firmwares the
  controls stay greyed out, the panel refuses the request and a saved 8 GiB setting is ignored. On 13.60 the panel
  shows that the firmware is pending testing.
- Notifications say "memory" instead of "budget" and "8 GiB enabled for PS4 games" instead of "for new NEO launches";
  the "no budget write needed" and "readback verified" notes are gone. Disabling 8 GiB says "original 5.25 GiB memory
  restored".

## 0.2.0

### 8 GiB PS4 Pro memory budget

Selects an 8 GiB PS4 Pro memory budget when the required kernel access is available. Detection uses
page permissions and verified kernel bytes, without depending on Byepervisor markers or a specific
hypervisor exploit. Without that access, the normal NEO feature remains available.

**The 8 GiB budget is disabled by default.** Enable it from the web panel and use Save Settings to
keep it across restarts. It is currently compatible with and tested on **firmware 13.20 only**.
Firmware 13.60 has verified offsets from kernel analysis but the application memory areas offset is
unverified and needs a hardware test. **If you are on 13.20 or 13.60, please test and report your
results.**

- Separate NEO and 8 GiB status indicators and controls. Disabling one keeps the other setting unchanged.
- Memory changes requested while a game is open wait until games are closed.
- Kernel layout checks, readback verification and rollback protect memory-budget changes. Unknown layouts are
  refused; normal NEO functionality remains available when the 8 GiB adjustment is unavailable.
- Shows each running PS4 game's reported DMEM budget.

### Persistent settings

Save Settings saves both NEO hook and 8 GiB state to `/data/neo-settings` on the PS5. Settings are
restored on the next manager startup. Without a settings file, NEO is enabled and 8 GiB is disabled.

### Firmware diagnostic

Save Diagnostic writes a JSONL metadata capture to `/data/neo-diagnostic.jsonl` on the PS5, for
extraction via FTP. The web panel also serves `GET /diagnostic` for direct access. This collects
firmware version, running games, driver presence and DMEM/FMEM queries without applying patches or
reading kernel code. Used to port the 8 GiB budget to other firmwares.

### System logs

Save Logs saves the last 1 MB of the kernel message buffer to `/data/neo-logs.txt` on the PS5, for
extraction via FTP. Show Logs opens the log in the browser.

### Web panel

- Renamed to NeoEnabler. New icon (black background, green text).
- Two-column grid layout with sticky header, inspired by PS4Mount.
- Save Settings and Quit moved to the header bar.
- Firmware version displayed in the header.
- 8 GiB controls are greyed out on unsupported firmwares.
- A second manager cannot change settings if the web-panel port is already occupied.

### Game patches

- New game: Driveclub (01.28), 1080p and 1440p in PS4 Pro mode, plus 4K with 1800p render and native 4K.
  **Driveclub 4K entries need the 8 GiB memory budget and are tested on firmware 13.20 only.**
- Bloodborne: native 4K in PS4 Pro mode (needs the 8 GiB memory budget, tested on firmware 13.20 only).
- The Order: 1886: new Native 4K 60fps entry with triple buffering. Vsync stays on (no tearing) and the frame rate goes
  up to 60 instead of dropping to 30. Uses the 60 FPS Unlock patch by illusion.
- The Order: 1886, Native 4K: fixed faint vertical stripes on characters under strong light.
- The Order: 1886, Native 4K: on-screen text, objectives and button prompts are no longer cut in the middle of the
  screen.
- The Order: 1886, Native 4K: fixed small black and grey blocks flickering on trees, bushes and other foliage, also
  visible as dark outlines on clothing edges.
- Bloodborne patches renamed to the same pattern as the other games ("1440p (PS4 Pro mode)" and so on). If you
  had one enabled, enable it again after updating the file.
- Patch descriptions in Bloodborne, Just Cause 3 and Resident Evil 6 rewritten in plain language.
- The game patch files only carry patches made in this project. Community patches (60 FPS, debug camera and
  others) were removed and stay available from their original authors.

### Build and CI

- The payload file is now named `neo-manager.elf`.
- Host unit tests cover page access, memory changes, automatic/manual controls and rollback.
- CI runs unit tests, firmware diagnostic toolkit tests, cppcheck, shellcheck, secret scan and CodeQL.
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
