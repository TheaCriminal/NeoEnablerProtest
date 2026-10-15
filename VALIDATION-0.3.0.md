# NeoEnabler 0.3.0 validation — 2026-10-15

ELF SHA256: `20f8810864214378e7fc3ef45ab883d890dc67ec528c58f0bbdc563b91d9b464`.

Host GPU lifecycle/settings tests and HTTP request tests passed. SDK build passed with warnings treated as errors. Existing budget and firmware-report tests passed during implementation. Extracted web JavaScript passed `node --check`; `git diff --check` passed.

Live PS5: firmware 13.20, Driveclub CUSA00093, NEO and 8192 MiB already enabled. Upgrading from the running 0.2.1 manager preserved both. An old settings file without GPU keys started GPU boost disabled, reporting 1800 MHz. Invalid target 2351 returned HTTP 400.

Applying 2200 reported enabled/applied with current 2200 and captured baseline 1800. Save Settings wrote the GPU keys. Quit requested baseline restoration; restarting 0.3.0 loaded the saved enabled target and applied 2200 again. Disable returned the reading to 1800. Final saved settings are NEO on, 8 GiB on, GPU boost off, target 2200.

The console Payload Manager ELF was replaced and read back byte-for-byte. Local regular and versioned artifacts are in `build/`. Unsupported firmware gating is covered by the fake-backend host tests, including rejection of saved enabled state; no second firmware was tested on hardware. Controls are disabled on unsupported firmware and the backend independently refuses enabling.

2350 MHz is an experimental selectable target, not a verified hardware maximum or gameplay stability result. This integration check applied only 2200 MHz. Current SMU readings do not establish performance gains.
