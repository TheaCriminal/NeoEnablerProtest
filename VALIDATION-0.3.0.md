# NeoEnabler 0.3.0 validation — 2026-10-15

Current ELF SHA256: `5a9f7fa0604eeb8a2dfa0257796b25ef4e6a759223d9ccab241196e8f8b3c5d5`.

Follow-up after the user again saw 1800: remote reads observed 2200 → 1800 → 2200 → 2146–2148 MHz, without a manual Enable. A subsequent 30-sample, one-second remote GET-only collection reported 2145–2148 MHz throughout (no 1800 samples). The user confirmed gameplay active with the panel closed during this collection. This verifies boost during that gameplay interval; it does not establish all future transitions or unlimited recovery. Raw samples: `/tmp/neo-gpu-transition-samples.json` on the development host.

1800 MHz recovery: observed the previous payload reporting enabled/target 2200 but actual 1800 after the user's browser transition. Added two-poll reset detection and a three-recovery budget per game/manual Enable. Host tests verify recovery, preserved baseline, budget exhaustion, no writes for 2155 MHz variation, and interrupted evidence on unknown scans. SDK build passed. Updated running payload and Payload Manager; preserved the user's unsaved enabled 2200 target without writing settings. The physical browser-to-game transition still needs user confirmation. This is a bounded reset heuristic, not foreground-state detection.

Panel navigation fix: form submissions use fetch and replace the document rather than creating POST history entries. Tab links use location.replace; Escape/BrowserBack keys request window.close while retaining native Back handling. Node tests cover consecutive actions, duplicate-submit suppression, tab replacement, and close keys. SDK build passed; the deployed panel script and Payload Manager ELF were verified byte-for-byte. Physical Circle behavior still needs confirmation on the console. Close and reopen an already-open browser session to discard its old history.

Host GPU lifecycle/settings tests and HTTP request tests passed. SDK build passed with warnings treated as errors. Existing budget and firmware-report tests passed during implementation. Extracted web JavaScript passed `node --check`; `git diff --check` passed.

Live PS5: firmware 13.20, Driveclub CUSA00093, NEO and 8192 MiB already enabled. Upgrading from the running 0.2.1 manager preserved both. An old settings file without GPU keys started GPU boost disabled, reporting 1800 MHz. Invalid target 2351 returned HTTP 400.

Applying 2200 reported enabled/applied with current 2200 and captured baseline 1800. Save Settings wrote the GPU keys. Quit requested baseline restoration; restarting 0.3.0 loaded the saved enabled target and applied 2200 again. Disable returned the reading to 1800. Final saved settings are NEO on, 8 GiB on, GPU boost off, target 2200.

The console Payload Manager ELF was replaced and read back byte-for-byte. Local regular and versioned artifacts are in `build/`. Unsupported firmware gating is covered by the fake-backend host tests, including rejection of saved enabled state; no second firmware was tested on hardware. Controls are disabled on unsupported firmware and the backend independently refuses enabling.

The maximum allowed target was reduced to 2200 MHz after the initial integration check. Host tests cover rejection of higher requests and saved settings. The rebuilt payload was deployed, the live page and JSON advertise 2200, and live requests for 2201, 2250 and 2350 returned HTTP 400. The Payload Manager copy was updated and read back byte-for-byte. GPU boost remains off. Current SMU readings do not establish performance gains or long-term stability.
