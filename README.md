# NEO Enabler

Play PS4 games on your PS5 in **PS4 Pro mode**, and with the included game patches, at higher resolutions up to
native 4K.

The PS5 normally runs PS4 games as a regular PS4. NEO Enabler makes them start as if they were on a PS4 Pro. The
patches in `xml/` then fix each game for Pro mode and raise its resolution.

## What you need

- A jailbroken PS5 on firmware 4.00 to 13.60 (tested on 13.20) with an ELF loader, such as etaHEN or elfldr.
- [ps-patch-system](https://github.com/illusionyy/ps-patch-system), loaded **before** NEO Enabler.

## Install

1. Download `neo-enabler-manager.elf` from the [latest release](https://github.com/gasb07/NeoEnabler/releases/latest).
2. After every console boot, load ps-patch-system, then send `neo-enabler-manager.elf` with your payload sender
   (port 9021).
3. A notification on the PS5 confirms that NEO Enabler is enabled.

Pro mode stays on until you turn it off or reboot the console. It applies to games you open afterwards.

## Control panel

Open `http://<PS5 IP>:8765` in a browser on your network, or the **NEO Enabler** shortcut in the PS5 Media tab.

- Turn PS4 Pro mode on or off.
- See which PS4 game is open and whether it is running in PS4 Pro mode.

Anyone on your local network can open the panel.

## Game patches

PS4 Pro mode alone is usually not enough: most games need their patch from `xml/`.

1. Copy the game's file to `/data/user/patches/xml_orbis/` on the PS5 and rename it to the title ID only, for
   example `Bloodborne_CUSA00900.xml` becomes `CUSA00900.xml`.
2. In the Patch Manager, enable **one** resolution entry for the game, for example "Native 4K (PS4 Pro mode)".
3. For 4K entries, set the PS5 video output to 2160p.

| Game (version) | Resolutions | Notes |
| --- | --- | --- |
| Need for Speed Rivals (01.04) | 1440p, 4K | Keep "Resolution Patch" off. "60 FPS Unlock" makes the car flicker for a few seconds when accelerating. |
| Resident Evil 6 (01.01) | 1080p, 1440p, 4K | Small Capcom logo in 4K. |
| Bloodborne (01.09) | 1440p, 1620p, 1800p | 4K is not possible: the game runs out of memory. |
| Just Cause 3 (01.05) | 1080p, 1440p, 4K | Also enable "Offline mode", or the game keeps trying to log in. "60 FPS" is optional. |
| Battlefield 4 (01.24) | 1080p, 1440p, 1620p, 1800p, 4K | 1800p holds 60 fps. 4K runs around 49 fps. |
| Killzone Shadow Fall (01.81) | 1440p, 1620p, 1800p, 4K | Some surfaces are slightly darker from 1440p to 1800p. 4K is clean but below 60 fps. |
| The Order: 1886 (01.02) | 1080p, 4K | Small sparkles may appear on edges while moving. |

The files also keep the community patches that already existed for each game, such as 60 FPS and debug camera.
Patch IDs, how the hook works and how to build from source: [docs/TECHNICAL.md](docs/TECHNICAL.md).

## Credits

### Code and tools

| Project | Author | Used for |
| --- | --- | --- |
| [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) | [John Törnblom](https://github.com/john-tornblom) | Toolchain (`prospero-clang`, `ld.lld`), runtime with `kernel_proc_copyin/out` and `kernel_dynlib_*`, `prospero-deploy` |
| [elfldr](https://github.com/ps5-payload-dev/elfldr) | [ps5-payload-dev](https://github.com/ps5-payload-dev) ([John Törnblom](https://github.com/john-tornblom)) | ELF loader on port 9021 |
| [ps-patch-system](https://github.com/illusionyy/ps-patch-system) | [illusion](https://github.com/illusionyy) | Patch server, XML format, the `getAppInfo` hook the shim chains onto, call site patterns and `ATTRIBUTE` offsets. `src/dynlib.h` and `src/proc.h` copied from `shared/` |
| [ps5-app-dumper](https://github.com/EchoStretch/ps5-app-dumper) | [EchoStretch](https://github.com/EchoStretch) | Reference for the `libSceAppInstUtil` load order and the Media tab shortcut (`deeplinkUri`) |
| [etaHEN](https://github.com/etaHEN/etaHEN) | [LightningMods](https://github.com/LightningMods) | Jailbreak, ELF loader, FTP and klog used during development and testing |
| [Pillow](https://github.com/python-pillow/Pillow) | [python-pillow](https://github.com/python-pillow) | Icon generation (`tools/make_icon.py`) |

### Game patches

| Patch | Author | Source |
| --- | --- | --- |
| Rivals: Resolution Patch, 60 FPS Unlock. Bloodborne: Skip Intro, Disable Motion Blur/Chromatic Aberration/vsync, Draw Color Border, 30 FPS Fix (with Lance McDonald). JC3: 60 FPS | [illusion](https://github.com/illusionyy) | [console-game-patches](https://github.com/illusionyy/console-game-patches) |
| Bloodborne: Resolution Patch WxH, base of the NEO 1440p/1620p/1800p entries | [Kyo](https://github.com/Kyoskii) | [shadps4-emu/ps4_cheats](https://github.com/shadps4-emu/ps4_cheats/blob/main/PATCHES/Bloodborne.xml) |
| Bloodborne: 60 FPS (With Deltatime), Resolution Patch (720p), Restore Debug Camera | Lance McDonald ([manfightdragon](https://github.com/manfightdragon)) | [shadps4-emu/ps4_cheats](https://github.com/shadps4-emu/ps4_cheats/blob/main/PATCHES/Bloodborne.xml) |
| Bloodborne: Restore Debug Dash | stagvant (no GitHub profile found) | [shadps4-emu/ps4_cheats](https://github.com/shadps4-emu/ps4_cheats/blob/main/PATCHES/Bloodborne.xml) |

Entries with author "Local experiment" (Pro mode, resolutions, JC3 offline mode) and the NEO fixes inside the
Bloodborne resolution entries were made in this project.

## License

GPL-3.0 (`LICENSE`). The payload includes the ps5-payload-sdk runtime (GPL-3.0-or-later), so
the binary must be distributed under GPL-3.0.

Third-party files keep their own licenses:

- `src/dynlib.h`, `src/proc.h`: Apache-2.0 (ps-patch-system), compatible with GPL-3.0.
- Patch entries from console-game-patches: MIT.
- Patch entries from shadps4-emu/ps4_cheats: the repository declares no license. They are included with
  attribution to their authors.
