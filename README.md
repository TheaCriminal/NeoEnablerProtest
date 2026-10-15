# NEO Enabler

Play PS4 games on your PS5 in **PS4 Pro mode** at higher resolutions, up to native 4K.

The PS5 already runs PS4 games that have PS4 Pro support in Pro mode. Games without it run as on a regular PS4.
NEO Enabler makes those games start as if they were on a PS4 Pro (games with Pro support are left as they are). This
alone does not make any game work: every game tested so far broke in Pro mode without its patch (black or
corrupted image, or the game closing). The patches in `xml/` fix each game for Pro mode and raise its resolution,
so NEO Enabler is only useful for the games listed below.

## What you need

- A jailbroken PS5 on firmware 4.00 to 13.60 (tested on 13.20) with an ELF loader, such as etaHEN or elfldr.
- [ps-patch-system](https://github.com/illusionyy/ps-patch-system), loaded **before** NEO Enabler.

## Install

1. Download `neo-manager.elf` from the [latest release](https://github.com/gasb07/NeoEnabler/releases/latest).
2. After every console boot, load ps-patch-system, then send `neo-manager.elf` with your payload sender
   (port 9021).
3. A notification on the PS5 confirms that NEO Enabler is enabled.

Version **0.2.0** also automatically enables an **8 GiB PS4 Pro memory budget** when the required kernel
access is available. This memory adjustment currently supports **firmware 13.20** and **13.60**. The hypervisor must already
be unlocked by Byepervisor or another method that allows reading and writing the kernel code page; NEO Enabler
checks that access without relying on exploit-specific markers and does not unlock the hypervisor itself.
Without that access, the normal NEO feature remains available.

Pro mode stays on until you turn it off or reboot the console. It applies to games you open afterwards.

## Control panel

Open `http://<PS5 IP>:8765` in a browser on your network, or the **NEO Enabler** shortcut in the PS5 Media tab.

- Turn PS4 Pro mode on or off.
- See whether the 8 GiB budget is enabled, disabled, or unavailable, and turn it on or off independently of NEO.
- See which PS4 game is open and whether it is running in PS4 Pro mode.
- See the DMEM budget reported for each running PS4 game (8192 MiB means 8 GiB).

The manager enables NEO on startup and automatically tries to select 8 GiB. If games are open or kernel access
is unavailable, it waits and checks again. **Close every game before changing the memory budget.** A memory
change requested while a game is open is applied after games close. Disabling 8 GiB stops automatic enabling
for this manager session; enabling it resumes that behavior. Disabling NEO leaves the memory setting as it is,
and disabling 8 GiB leaves NEO as it is. Quitting the manager keeps both patches but stops automatic checks.
Starting the manager again restores its startup behavior. Rebooting removes both patches.

The 8 GiB setting changes the budget for new PS4 Pro launches. Existing games keep their launch budget, and
each game decides how much to allocate. It does not change resolution or force a game's internal memory pools
to grow. Driveclub gameplay with a memory-using patch was reported working in the local 13.20 test; this is not
a compatibility guarantee for other games. Restoring the original budget selects the captured 5.25 GiB NEO
configuration. A failed kernel-write recovery requires a console reboot before launching games.

Use one manager at a time. If you used the experimental panel on port 8766, quit it with **Quit the manager**
before loading this version. Version 0.2.0 uses the regular panel and Media shortcut on port **8765**.

Anyone on your local network can open the panel.

## Game patches

Every game needs its patch from `xml/` to work in PS4 Pro mode. Turn PS4 Pro mode off in the panel before playing
a game that is not in the table below.

1. Copy the game's file to `/data/user/patches/xml_orbis/` on the PS5 and rename it to the title ID only, for
   example `Bloodborne_CUSA00900.xml` becomes `CUSA00900.xml`.

   If ps-patch-system already has a file for that game (`/data/user/patches/xml_orbis/<TITLE ID>.xml`, for example
   `/data/user/patches/xml_orbis/CUSA00900.xml`), don't replace it. Open the game's file from `xml/`, copy every
   `<Metadata ...>` block (each one holds its own `<PatchList>`), and paste them into the existing file, before
   the closing `</Patch>` line. Each `<PatchList>` must stay inside its `<Metadata>` block, or the patch system
   won't load it.
2. In the Patch Manager, enable **one** resolution entry for the game, for example "Native 4K (PS4 Pro mode)".
3. For 4K entries, set the PS5 video output to 2160p.

| Game (version) | Resolutions | Notes |
| --- | --- | --- |
| Need for Speed Rivals (01.04) | 1440p, 4K | If your existing patch file for the game has "Resolution Patch", keep it off. |
| Resident Evil 6 (01.01) | 1080p, 1440p, 4K | Small Capcom logo in 4K. |
| Bloodborne (01.09) | 1440p, 1620p, 1800p, native 4K | Native 4K needs the 8 GiB memory budget. **Tested on firmware 13.20 only.** |
| Just Cause 3 (01.05) | 1080p, 1440p, 4K | Also enable "Offline mode", or the game keeps trying to log in. |
| Battlefield 4 (01.24) | 1080p, 1440p, 1620p, 1800p, 4K | 1800p holds 60 fps. 4K runs around 49 fps. |
| Killzone Shadow Fall (01.81) | 1440p, 1620p, 1800p, 4K | Some surfaces are slightly darker from 1440p to 1800p. 4K is clean but below 60 fps. |
| The Order: 1886 (01.02) | 1080p, 4K, triple buffering up to 60 fps (4K) | Small sparkles may appear on edges while moving. The triple buffering entry is used together with Native 4K: no tearing, slight stutter. |
| Driveclub (01.28) | 1080p, 1440p, 4K (1800p render), native 4K | 4K needs the 8 GiB memory budget. 4K with 1800p render runs close to 60 fps; native 4K is below 60 fps in races. Snow can show inside the car in 4K. |

Patch IDs, how the hook works and how to build from source: [docs/TECHNICAL.md](docs/TECHNICAL.md).

## Helping extend 8 GiB to other firmwares

The 8 GiB budget only works on firmware **13.20** right now. To port it we need a `diagnostic.jsonl` capture
and, ideally, a raw kernel TEXT dump from each firmware. Build and run `diagnostics/neo_diagnostic.c` on your
console, save the full stdout and see [diagnostics/README.md](diagnostics/README.md) for details on what files
to submit.

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
| Bloodborne: Resolution Patch WxH, base of the NEO 1440p/1620p/1800p entries | [Kyo](https://github.com/Kyoskii) | [shadps4-emu/ps4_cheats](https://github.com/shadps4-emu/ps4_cheats/blob/main/PATCHES/Bloodborne.xml) |
| The Order: 1886: 60 FPS Unlock (one line inside the triple buffering entry) | [illusion](https://github.com/illusionyy) | PS-Game-Patch, `patches/xml/TheOrder1886-Orbis.xml` |

Entries with author "Local experiment" (Pro mode, resolutions, JC3 offline mode) and the NEO fixes inside the
Bloodborne resolution entries were made in this project.

## License

GPL-3.0 (`LICENSE`). The payload includes the ps5-payload-sdk runtime (GPL-3.0-or-later), so
the binary must be distributed under GPL-3.0.

Third-party files keep their own licenses:

- `src/dynlib.h`, `src/proc.h`: Apache-2.0 (ps-patch-system), compatible with GPL-3.0.
- The Bloodborne resolution code from shadps4-emu/ps4_cheats: the repository declares no license. It is included
  with attribution to its author.
