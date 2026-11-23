# Bottle app (Flutter)

A responsive Material companion for bottle protocol v2. The Dart entry point keeps the `BottleTesterApp` name for compatibility. It runs on macOS; Android/iOS platform folders and BLE permissions are preserved/included, but mobile hardware has not been validated.

## Open the normal standalone Mac app

Use the Release bundle, not the Debug bundle that integration tests overwrite:

```bash
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer ~/flutter/bin/flutter build macos --release --target lib/main.dart
open build/macos/Build/Products/Release/bottle_tester.app
```

An integration-test build in `build/macos/Build/Products/Debug/` can show a blank window when opened directly because it is a test runner, not the normal app entry point. Rebuild with the explicit normal target above.

## Run for development

```bash
export PATH="$HOME/flutter/bin:$PATH"
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
cd bottle_tester
flutter pub get
flutter run -d macos
```

Flash protocol v2 firmware first. See `../docs/PROTOCOL.md` for the shared contract. Legacy text-command firmware is not supported.

## Everyday use

- **Home:** find and explicitly connect to your bottle, see live daily water, last sip, scale status, next check and connection health. Weight-based water totals are estimates, not medical measurements.
- **Setup:** choose a fixed UTC offset; sync from internet via up to four 2.4 GHz Wi-Fi networks, or explicitly use the app clock fallback. Tare the **empty base**, then calibrate with a known reference weight. Choose your reminder window and interval. Firmware factory schedule is **10:00–23:00, every 30 minutes**, but the app reads the actual device values rather than assuming these defaults.
- **Settings:** independent sip-reminder and missing-bottle colors, sound, persistent sound mute (lights still work), away timeout, beep delay, whole-gram sip threshold and scale settling time. Edits are drafts until saved. Pending writes and failures are visible; completion requires an ACK and fresh status readback.
- **Advanced:** all reported NVS keys (including unknown/new keys), effective values versus actual persistence, optional/legacy/unreported classifications, raw/live status, confirmed Classic target clear (BLE peer remains diagnostic), and bounded sanitized command/event logs. Unstored usable defaults are not faults. Tare/calibration are required scale setup values; `classic_mac` is required for alerts, `classic_name` is optional. BLE peer/Wi-Fi/baseline/legacy keys are optional/diagnostic.

**Wi-Fi setup:** use **Search networks** to discover 2.4 GHz networks from the bottle’s location, strongest signal first with security labels. Select a name, enter its password, then explicitly **Save and sync**. Searching/selection never saves or connects; changing the name clears the previous draft password. **Hidden / manual network** expands exact-name entry. Retry near the router if the list is empty or search fails; 5 GHz-only networks are unsupported. Scan results are published only after the matching ACK and `wifi.scan` result, in either order, with a **25-second** deadline. Search is unavailable during an existing command or internet sync. See `../docs/WIFI_SCAN.md` for the additive contract.

Wi-Fi names/passwords retain spaces and UTF-8; credentials use JSON rather than a space-delimited command. Empty password means an open network. Saving Wi-Fi is not evidence of internet success: only a later `wifi_state: synced` confirms internet time. Wi-Fi errors remain visible. The UTC offset is fixed, not an IANA timezone; update it for daylight saving changes.

## Classic phone setup

In Setup, open your phone’s **system Bluetooth settings** for first discovery, then **Search phones**. This commands the **bottle’s Classic inquiry**, not a BLE phone scan. Results show names (including clearly labelled unnamed devices), RSSI and secondary diagnostic addresses. There is no manual MAC field. Up to 8 unique strongest results are shown. An empty scan is valid and never selects anything.

Tap your intended phone, check the exact displayed name/address and explicitly confirm **Save Classic phone** or **Replace Classic phone**. The app sends only `address` and the current completed `scan_id`; stale/other-session discoveries cannot be selected. Selection is never automatic. Saving verifies status readback, clears old reachability, and is **not** proof of presence. **Check selected phone** waits for both a correlated positive ACK and `phone.probe` result, in either order, with a 30-second deadline (same for `phone.scan`). Busy, malformed, negative ACK, radio errors, timeouts and link loss never imply presence. **Clear selected Classic phone** requires confirmation and verifies readback; it removes only the Classic target, not the BLE diagnostic peer, Wi-Fi or calibration.

Presence remains always-on/read-only. Alerts need a fresh targeted Classic remote-name reply; manual positive checks do not authorize later alerts. Home distinguishes **app BLE link (configuration only)** from **selected phone Classic presence**. `phone_mac` is now the selected Classic target, `phone_name` its label, and `app_peer_mac` the separate raw BLE peer diagnostic. Legacy BLE-only status never establishes Classic presence. No target means actionable setup and no alerts. Target persists in NVM, but reboot requires a new reachability check. There is no BLE disconnect grace or requirement to keep the app open.

Targeted Classic remote-name checks were hardware-tested on ESP with a **locked Samsung phone in the background**. **iPhone hardware testing is pending; compatibility is not universal.** First discovery can require the Bluetooth settings screen even when subsequent checks do not. See `../docs/CLASSIC_PRESENCE_PROTOCOL.md`.

## BLE and privacy

- Notification subscription is established before commands. Incoming bytes reassemble through newline (4 KiB bound, UTF-8 safe, malformed/oversized frame recovery).
- Exactly one request at a time; serial ≤20-byte GATT writes bounded by MTU. Matching request IDs and positive ACKs are required. Default ACK deadline is **15 seconds**; complete NVS dump deadline is **30 seconds**. Status commands require both ACK and snapshot; NVS requires ACK and `nvs.end`.
- Firmware pushes status every two seconds. A two-second watchdog requests a snapshot after two missing pushes and marks stale data after six seconds.
- Lost/failing/canceled connections clean up subscriptions, pending commands and timers. Scanning never claims a peer. Reconnect is opt-in for an exact remembered bottle: once at launch and at most one retry following loss, not an infinite scan/connect loop. Manual disconnect cancels retries.
- Connecting may capture a raw BLE peer for diagnostics only. It never configures Classic presence or establishes ownership. Only connect to your intended bottle.
- `settings.presence_enabled` remains read-only `true`; attempts to write it (even `true`) are rejected. Never infer presence from an app connection or a saved identity.
- Passwords/identity keys are never read back or logged. Native/Dart FlutterBluePlus packet logging is disabled before BLE operations because its debug mode can expose raw GATT bytes. Only the opted-in bottle identifier/name is saved locally; Wi-Fi credentials are not stored by the app.
- The existing `License.nonprofit` FlutterBluePlus setting is appropriate for this personal prototype. **Commercial distribution requires reviewing/purchasing the plugin's commercial license** and selecting its commercial license mode.

## Verification (no hardware)

```bash
dart format lib test integration_test
flutter analyze
flutter test
flutter build macos --debug
```

`BottleTransport` and `SessionStore` are injectable. Unit/widget tests use an in-memory peer, not platform Bluetooth, and cover framing, ACK rejection/correlation/timeouts, queue ordering, NVS defaults, credentials/spaces, readback, lifecycle/reconnect, confirmation flows, and 390px/desktop layouts.

## Opt-in real BLE boundary test

**Do not run before flashing matching firmware and intentionally selecting the test bottle.** This test really connects and therefore triggers prototype peer capture. It never tares/calibrates, changes Wi-Fi, or clears identity. It reads status/NVS, scans Classic devices without selecting any identity, probes only an already configured target, runs a real non-saving Wi-Fi scan (an empty list is valid), checks signal ordering and unchanged saved network/credential metadata, checks safe diagnostic redaction, checks a negative ACK for an invalid command, and disconnects in `finally`.

```bash
flutter test integration_test/ble_hardware_test.dart -d macos \
  --dart-define=HARDWARE_TEST=true
```

It requires exactly one discovered candidate; with multiple bottles, add `--dart-define=BOTTLE_DEVICE_ID=<exact-platform-id>`. The test is skipped without `HARDWARE_TEST=true`. **Classic selection is off by default and never chooses the first/random discovered phone.** Persistent Classic selection/replacement requires BOTH `--dart-define=SELECT_CLASSIC_PHONE=true` and `--dart-define=CLASSIC_PHONE_ADDRESS=AA:BB:CC:DD:EE:FF` for your exact intended phone, which must appear in the current scan. This intentionally changes the saved target and is not automatically restored (restoration requires a new discovery and explicit confirmation). Leave the flag off for read-only identity testing. Optionally add `--dart-define=MUTATE_DEVICE=true` to toggle DND and restore its original value in `finally`; leave this off for read-only smoke testing. If the link is lost during restoration, the failure explains that manual restoration is needed. The test deliberately has no infinite reconnect behavior.
