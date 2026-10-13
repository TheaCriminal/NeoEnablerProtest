# NEO Enabler: technical notes

Details for developers. The user guide is in the [README](../README.md).

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
5. Disabling also removes the hook of NEO Enabler v4 when it is still in `SceShellCore` (fw 13.20). v4 overwrote
   124 bytes of ps-patch-system code (`eh_frame + 0x5cf`) and redirected the call at `eh_frame + 0x6bc` to them, so
   games kept launching in Pro mode while the panel showed "disabled". The original bytes are restored only when
   the exact v4 bytes are found (`src/legacy_v4.h`).
6. On first run it installs the `NEOE00001` ("NEO Enabler") shortcut in the Media tab, which opens the panel.

The shim overwrites unwind data at the end of `eh_frame`, as ps-patch-system does at its start. Rebooting the
console restores it.

Pro mode alone does not make any game work: every game tested broke without its fixes in `xml/` (display buffer
too small, corrupted layout, GPU hangs).

## Build

Dependencies: [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) (default `/opt/ps5-payload-sdk`, or set
`PS5_PAYLOAD_SDK`), `python3`, `xxd`.

```sh
./build.sh                  # builds build/neo-manager.elf
python3 tools/make_icon.py  # optional: regenerates assets/icon0.png (needs Pillow and the DejaVu Sans Bold font)
```

| File | Contents |
| --- | --- |
| `src/manager.c` | Payload: web panel, notification, Media tab shortcut |
| `src/shellcore_hook.h` | Call site scan, cave, install and removal of the hook |
| `src/legacy_v4.h` | Detection and removal of the NEO Enabler v4 hook |
| `src/shim.S`, `src/shim.ld` | Hook code, linked at 0 and copied to the cave |
| `src/dynlib.h`, `src/proc.h` | Headers from ps-patch-system (`shared/`) |
| `tools/gen_shim.py` | Generates `build/shim_data.h` (shim bytes and data field offsets) |
| `tools/make_icon.py` | Draws `assets/icon0.png` |
| `xml/` | Game patches |

## Releases

GitHub Actions (`.github/workflows/build.yml`) builds every push and pull request with the PS5 Payload SDK
release set in `SDK_VERSION`, and keeps `neo-manager.elf` as a build artifact.

To publish a release:

1. Set the new version in `src/manager.c` (`#define VERSION "NEO Enabler manager X.Y.Z"`) and commit it.
2. Tag the commit and push the tag:
   ```sh
   git tag vX.Y.Z
   git push origin vX.Y.Z
   ```

The workflow checks that the tag matches `VERSION`, builds the payload and creates the GitHub release with
`neo-manager.elf` and its SHA-256 checksum attached.

## Web panel

| Route | Effect |
| --- | --- |
| `GET /` | Hook state, open PS4 games (NEO or base), Enable, Disable and Quit buttons |
| `POST /enable`, `POST /disable` | Installs or removes the hook (disable also removes the v4 hook) and notifies on the PS5 |
| `POST /quit` | Stops the panel and keeps the hook state |
| `GET /status` | Plain text: hook state, v4 hook (`present` or `absent`), firmware, call site, cave and open games |

- Starting the manager enables the hook. The panel shows "enabled" while the v4 hook is present.
- Changes apply to games launched afterwards. Rebooting the console removes the hook and the panel.
- Sending the payload again while the panel runs reinstalls the hook if needed and reports that the panel is
  already running.
- Open games: `eboot.bin` processes that load a PS4 graphics driver, named from the `TITLE` in
  `/system_data/priv/appmeta/<title ID>/param.sfo`. `libSceGnmDriverForNeoMode` means Pro mode and `libSceGnmDriver`
  means base mode. Some games load `libSceGnmDriverCompat1` instead (The Order: 1886 on 13.20, while Battlefield 4
  loads the Pro driver on the same firmware), with the same name in both modes, and then the mode comes from the
  `kPS4Pro` bit of the launch attribute (offset 0x28 of `sceKernelGetAppInfo`, checked on 13.20).
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
| Just Cause 3 01.05 | `JustCause3_CUSA01493.xml` | one resolution: `184cc682` 1080p, `1575f9b2` 1440p or `9644fb2b` 4K, + `fd9434a8` Offline mode | 1080p, 1440p or native 4K |
| Battlefield 4 01.24 | `Battlefield4_CUSA00049.xml` | one resolution: `f4d53c61` 1080p, `f9ec0351` 1440p, `079ce771` 1620p, `3ad0f441` 1800p or `7add01c8` 4K | 1080p (900p render), 1440p, 1620p, 1800p (stable 60 fps) or native 4K (~49 fps) |
| Killzone Shadow Fall 01.81 | `KillzoneShadowFall_CUSA00190.xml` | one resolution: `8c299cea` 1440p, `725978ca` 1620p, `4f156bfa` 1800p or `0f189e73` Native 4K | 1440p, 1620p, 1800p (slight darkening on some surfaces) or native 4K (clean, below 60 fps) |
| The Order: 1886 01.02 | `TheOrder1886_CUSA00785.xml` | one resolution: `ed70c1da` 1080p or `6378fc73` Native 4K | 1080p or native 4K (small one-frame edge sparkles may remain while moving) |

- Each resolution entry already includes the Pro mode fixes. Enable only one per game.
- Rivals: keep the community "Resolution Patch" disabled if it is in the patch file.
- JC3: the Offline mode entry is required. Without it the game loops on the online login.
- The XMLs only carry entries made in this project. The Bloodborne entries build on Kyo's resolution patch.

