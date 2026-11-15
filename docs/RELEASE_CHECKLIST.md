# Release gates — not a claim of certification

## Engineering gates

- [ ] Firmware/app versions built together with protocol version checked before mutations.
- [ ] Host engine tests, storage failure tests, app framing/controller/widget tests pass.
- [ ] Minimum MTU 23 (20 payload bytes), split Unicode, reconnect mid-frame and ACK timeout tests.
- [ ] Cold-boot internet sync and app-clock fallback tested on actual networks, wrong password, no internet and no saved Wi-Fi.
- [ ] Saved zone verified east/west of UTC, sub-hour offsets, travel and daylight-saving instructions.
- [ ] Physical lift stops sound promptly; no-drink return resumes; actual drink/refill works; moving water never registers a false drink.
- [ ] Long missing-bottle alarm, quiet-hour transition, DND and presence transition checked acoustically and visually.
- [ ] Sensor disconnect/full scale/stale data silences all alerts; recovery verified.
- [ ] Calibration persists through update/reboot/power removal; fresh unit never auto-tares.
- [ ] Safe flash update/recovery and power-loss testing during every persistent operation.
- [ ] Long-duration soak (days, not minutes), millis rollover, memory usage, network failure soak.

## Product gates

- [ ] Factory calibration and load-cell mounting/tolerance strategy; end-user guided calibration only as recovery.
- [ ] Physical pairing/reset action and authenticated, encrypted BLE ownership. Current peer auto-replacement is prototype-only.
- [ ] Protect Wi-Fi credentials at rest (flash/NVS encryption and secure boot policy); no passwords in diagnostics/logs.
- [ ] Signed update, rollback/recovery, privacy policy and explicit owner transfer/factory reset.
- [ ] Review third-party commercial licenses, especially FlutterBluePlus; the current development app uses its nonprofit mode and is not licensed by this code change for commercial sale.
- [ ] Android and iOS real-device permission, background, OS update, reconnect and multi-phone tests.
- [ ] Mobile background presence policy validated before advertising away/home sensing. A laptop left on is not user presence.
- [ ] No colour-only status communication; text/accessibility labels, large text, contrast and screen-reader testing.
- [ ] Buzzer current, driver transistors, LED resistors, power supply/brownout, ESD, strain/water ingress checks.
- [ ] GPIO12 bootstrap levels, cold boot and enclosure thermal/mechanical behavior validated on the final PCB.
- [ ] Radio/electrical compliance and market-specific consumer product obligations reviewed professionally.
- [ ] Clear limitations: poured-out water looks like drinking; no medical claims; no automatic DST with a fixed UTC offset.
