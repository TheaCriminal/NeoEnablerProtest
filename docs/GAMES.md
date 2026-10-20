# Supported games

Games with a NeoEnabler patch in `xml/`, the resolutions available and notes for each one. How to install
and enable the patches: [Game patches](../README.md#game-patches).

| Game (version) | Resolutions | Notes |
| --- | --- | --- |
| Need for Speed Rivals (01.04, 01.03) | 1440p, 4K | If your existing patch file for the game has "Resolution Patch", keep it off. |
| Resident Evil 6 (01.01) | 1440p, 4K | Small Capcom logo in 4K. |
| Bloodborne (01.09) | 1440p, 1620p, 1800p, native 4K | 1800p and native 4K need 8GB Mode. **Tested on firmware 13.20 only.** |
| Just Cause 3 (01.05) | 1440p, 4K | Also enable "Offline mode", or the game keeps trying to log in. |
| Battlefield 4 (01.24) | 1440p, 1620p, 1800p, 4K | 1800p holds 60 fps. 4K runs around 49 fps. |
| Battlefield Hardline (01.09) | 1440p, 1620p, 1800p, 4K | |
| Killzone Shadow Fall (01.81) | 1440p, 1620p, 1800p, 4K | Some surfaces are slightly darker from 1440p to 1800p. 4K is clean but below 60 fps. |
| The Order: 1886 (01.02) | 4K, 4K up to 60 fps | Small sparkles may appear on edges while moving. Native 4K 60fps uses triple buffering: no tearing, slight stutter. |
| Dark Souls III: The Fire Fades Edition (01.03) | 1440p, 1620p, 1800p, native 4K | The game already runs in PS4 Pro mode. Native 4K needs 8GB Mode. **Tested on firmware 13.20 only.** |
| Batman: Arkham Knight (01.15) | 1620p, 1800p, native 4K | |
| Sleeping Dogs: Definitive Edition (01.01) | 1440p, 1620p, 1800p, native 4K, each also up to 60 fps | The 60 fps entries use triple buffering: no tearing, slight stutter. 1800p up to 60 fps runs at 53-57 fps, native 4K up to 60 fps at 41-44 fps. |
| Driveclub (01.28) | 1440p, 4K (1620p render), 4K (1800p render), native 4K | 4K needs the 8 GiB memory budget. **Tested on firmware 13.20 only.** 4K with 1800p render runs close to 60 fps; native 4K is below 60 fps in races. 4K with 1620p or 1800p render may show blocky artifacts in the clouds and on other surfaces. Native 4K: snow can show inside the car. |
| Uncharted: The Nathan Drake Collection (01.02) | 1440p, 1620p, 1800p, native 4K for each of the three games | **Requires NeoEnabler 0.3.3 or newer.** The collection starts in Uncharted 1, which sets PS4 Pro mode for Uncharted 2 and 3: an Uncharted 2 or 3 entry needs an Uncharted 1 entry on (the panel enables it). Native 4K fits the stock memory with a smaller texture pool. Each game also has a native 4K entry with 8GB Mode and the full texture pool (**tested on firmware 13.20 only**). |

Test results by game and firmware: [COMPATIBILITY.md](COMPATIBILITY.md).
