# NEO Enabler

PS5 payload that makes PS4 games launch in PS4 Pro (NEO) mode. It installs a hook in `SceShellCore`, shows a
notification on the console and stays resident with a web panel on port 8765 to enable or disable the hook.
The game patches in `xml/` fix each game in Pro mode and unlock higher resolutions.

Supported firmware: the PS5 firmwares supported by ps-patch-system, **4.00 to 13.60**. Tested on 13.20. On other
firmwares the panel shows "unsupported firmware or ShellCore" and nothing is written.

## How it works

ps-patch-system copies its code to the start of the `SceShellCore` `eh_frame`, makes the module RWX and redirects
the `getAppInfo` call to its own hook. NEO Enabler chains onto that hook:

1. The manager finds the `getAppInfo` call with the same per-firmware patterns as ps-patch-system
   (`patchGetAppInfoSfo`). The call must point inside `eh_frame` (the ps-patch-system hook), otherwise the panel shows
   "ps-patch-system not loaded".
2. The shim (`src/shim.S`, position independent) is copied to the last 512 bytes of `eh_frame` and the call is
   redirected to it. The shim calls the ps-patch-system hook first, then, for PS4 title IDs whose `ATTRIBUTE` is
   exactly 2, sets bit `0x00800000` (`kPS4Pro`) and logs `[NEO <title>] old>new` to klog.
3. Per-firmware values are filled into the shim at install time: the previous call target, `printf`, the
   `ATTRIBUTE` offset in the app info (`0x26c` up to 10.60, `0x1f4` for 11.00 to 13.00, `0x214` from 13.20, as in
   ps-patch-system `get_ps5_attr`) and whether the app info is the third or fourth argument (fourth from 9.00).
4. Every write happens with `SceShellCore` stopped (SIGSTOP) and is read back. Disabling points the call back to
   the ps-patch-system hook.
5. On first run it installs the `NEOE00001` ("NEO Enabler") shortcut in the Media tab, which opens the panel.

The shim overwrites unwind data at the end of `eh_frame`, as ps-patch-system does at its start. Rebooting the
console restores it.

Pro mode alone is not enough: most games need the game-specific fixes in `xml/`.

## Requirements

- PS5 on a firmware supported by ps-patch-system, with an ELF loader on port 9021 (etaHEN or elfldr).
- Patch system ([ps-patch-system](https://github.com/illusionyy/ps-patch-system), `patch-server.elf`) loaded
  **before** the manager.

## Build

Dependencies: [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) (default `/opt/ps5-payload-sdk`, or set
`PS5_PAYLOAD_SDK`), `python3`, `xxd`.

```sh
./build.sh                  # builds build/neo-enabler-manager.elf
python3 tools/make_icon.py  # optional: regenerates assets/icon0.png (needs Pillow and the DejaVu Sans Bold font)
```

| File | Contents |
| --- | --- |
| `src/manager.c` | Payload: web panel, notification, Media tab shortcut |
| `src/shellcore_hook.h` | Call site scan, cave, install and removal of the hook |
| `src/shim.S`, `src/shim.ld` | Hook code, linked at 0 and copied to the cave |
| `src/dynlib.h`, `src/proc.h` | Headers from ps-patch-system (`shared/`) |
| `tools/gen_shim.py` | Generates `build/shim_data.h` (shim bytes and data field offsets) |
| `tools/make_icon.py` | Draws `assets/icon0.png` |
| `xml/` | Game patches |

## Usage

1. Load the patch system.
2. Send the payload once after the console boots:
   ```sh
   /opt/ps5-payload-sdk/bin/prospero-deploy -h <PS5 IP> -p 9021 build/neo-enabler-manager.elf
   ```
3. The notification shows `NEO Enabler enabled` and `Web panel: port 8765`.
4. Open `http://<PS5 IP>:8765` or the "NEO Enabler" shortcut in the Media tab.

| Route | Effect |
| --- | --- |
| `GET /` | Hook state, open PS4 games (NEO or base), Enable, Disable and Quit buttons |
| `POST /enable`, `POST /disable` | Installs or removes the hook and notifies on the PS5 |
| `POST /quit` | Stops the panel and keeps the hook state |
| `GET /status` | Plain text: hook state, firmware, call site, cave and open games |

- Changes apply to games launched afterwards. Rebooting the console removes the hook and the panel.
- Sending the payload again while the panel runs reinstalls the hook if needed and reports that the panel is
  already running.
- The panel has no authentication: any device on the local network can use it.

## Game patches

The files in `xml/` use the ps-patch-system format. The patch system looks for `<TITLE ID>.xml`, so rename the
file when copying it to `/data/user/patches/xml_orbis/`, for example `Bloodborne_CUSA00900.xml` becomes
`CUSA00900.xml`. Enable the entries in the Patch Manager or in `!_settings.xml`. Hashes are computed by the patch system from the file
name, entry name, title, ELF and app version, so renaming an entry changes its hash.

| Game | File | Enable | Result |
| --- | --- | --- | --- |
| Need for Speed Rivals 01.04 | `NeedForSpeedRivals_CUSA00168.xml` | one resolution: `7f4ee42f` 4K or `fc7fe6b6` 1440p | native 4K or 1440p |
| Resident Evil 6 01.01 | `ResidentEvil6_CUSA03856.xml` | one resolution: `8b70a2e1` 1080p, `86499dd1` 1440p or `05789f48` 4K | 1080p, 1440p or native 4K (small Capcom logo in 4K) |
| Bloodborne 01.09 | `Bloodborne_CUSA00900.xml` | one resolution: `bc972b87` 1800p, `89b0ba88` 1620p or `673a9e3d` 1440p | up to 1800p (native 4K blocked by memory) |
| Just Cause 3 01.05 | `JustCause3_CUSA01493.xml` | one resolution: `184cc682` 1080p, `1575f9b2` 1440p or `9644fb2b` 4K, + `fd9434a8` Offline mode + `66794a5c` 60 FPS | 1080p, 1440p or native 4K |
| Battlefield 4 01.24 | `Battlefield4_CUSA00049.xml` | one resolution: `f4d53c61` 1080p, `f9ec0351` 1440p, `079ce771` 1620p, `3ad0f441` 1800p or `7add01c8` 4K | 1080p (900p render), 1440p, 1620p, 1800p (stable 60 fps) or native 4K (~49 fps) |
| Killzone Shadow Fall 01.81 | `KillzoneShadowFall_CUSA00190.xml` | one resolution: `8c299cea` 1440p, `725978ca` 1620p, `4f156bfa` 1800p or `0f189e73` Native 4K | 1440p, 1620p, 1800p (slight darkening on some surfaces) or native 4K (clean, below 60 fps) |

- Each resolution entry already includes the Pro mode fixes. Enable only one per game.
- Rivals: keep "Resolution Patch" disabled. "60 FPS Unlock" makes the car flicker during the first seconds of
  acceleration.
- JC3: the Offline mode entry is required. Without it the game loops on the online login.
- The XMLs also keep the community entries that already existed for each game (60 FPS, debug camera, etc.).

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

GPL-3.0 (`LICENSE`). The payload statically links the ps5-payload-sdk runtime (`crt1.o`, GPL-3.0-or-later), so
the binary must be distributed under GPL-3.0.

Third-party files keep their own licenses:

- `src/dynlib.h`, `src/proc.h`: Apache-2.0 (ps-patch-system), compatible with GPL-3.0.
- Patch entries from console-game-patches: MIT.
- Patch entries from shadps4-emu/ps4_cheats: the repository declares no license. They are included with
  attribution to their authors.
