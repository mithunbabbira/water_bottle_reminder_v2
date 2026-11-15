# Bottle app (Flutter)

A responsive Material companion for bottle protocol v2. The Dart entry point keeps the `BottleTesterApp` name for compatibility. It runs on macOS; Android/iOS platform folders and BLE permissions are preserved/included, but mobile hardware has not been validated.

## Run

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
- **Advanced:** all reported NVS keys (including unknown/new keys), effective values versus actual persistence, optional/legacy/unreported classifications, raw/live status, diagnostic peer clear, and bounded sanitized command/event logs. Unstored usable defaults are not faults. Tare/calibration are required scale setup values; peer/Wi-Fi/baseline/legacy keys are optional.

**Wi-Fi setup:** use **Search networks** to discover 2.4 GHz networks from the bottle’s location, strongest signal first with security labels. Select a name, enter its password, then explicitly **Save and sync**. Searching/selection never saves or connects; changing the name clears the previous draft password. **Hidden / manual network** expands exact-name entry. Retry near the router if the list is empty or search fails; 5 GHz-only networks are unsupported. Scan results are published only after the matching ACK and `wifi.scan` result, in either order, with a **25-second** deadline. Search is unavailable during an existing command or internet sync. See `../docs/WIFI_SCAN.md` for the additive contract.

Wi-Fi names/passwords retain spaces and UTF-8; credentials use JSON rather than a space-delimited command. Empty password means an open network. Saving Wi-Fi is not evidence of internet success: only a later `wifi_state: synced` confirms internet time. Wi-Fi errors remain visible. The UTC offset is fixed, not an IANA timezone; update it for daylight saving changes.

## BLE and privacy

- Notification subscription is established before commands. Incoming bytes reassemble through newline (4 KiB bound, UTF-8 safe, malformed/oversized frame recovery).
- Exactly one request at a time; serial ≤20-byte GATT writes bounded by MTU. Matching request IDs and positive ACKs are required. Default ACK deadline is **15 seconds**; complete NVS dump deadline is **30 seconds**. Status commands require both ACK and snapshot; NVS requires ACK and `nvs.end`.
- Firmware pushes status every two seconds. A two-second watchdog requests a snapshot after two missing pushes and marks stale data after six seconds.
- Lost/failing/canceled connections clean up subscriptions, pending commands and timers. Scanning never claims a peer. Reconnect is opt-in for an exact remembered bottle: once at launch and at most one retry following loss, not an infinite scan/connect loop. Manual disconnect cancels retries.
- This prototype **automatically captures the connecting phone peer**. The connect dialog explains this. It is not secure ownership or a production security feature. Only connect to your own intended bottle.
- Optional presence gating uses an actual BLE link, with the firmware's 60-second disconnect grace and no boot grace. Stored MAC addresses are not passive presence proof. Closing/backgrounding the app can suppress alerts; standalone users should leave the gate disabled.
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

**Do not run before flashing matching firmware and intentionally selecting the test bottle.** This test really connects and therefore triggers prototype peer capture. It never tares/calibrates, changes Wi-Fi, or clears identity. It reads status/NVS, runs a real non-saving Wi-Fi scan (an empty list is valid), checks signal ordering and unchanged saved network/credential metadata, checks safe diagnostic redaction, checks a negative ACK for an invalid command, and disconnects in `finally`.

```bash
flutter test integration_test/ble_hardware_test.dart -d macos \
  --dart-define=HARDWARE_TEST=true
```

It requires exactly one discovered candidate; with multiple bottles, add `--dart-define=BOTTLE_DEVICE_ID=<exact-platform-id>`. The test is skipped without `HARDWARE_TEST=true`. Optionally add `--dart-define=MUTATE_DEVICE=true` to toggle DND and restore its original value in `finally`; leave this off for read-only smoke testing. If the link is lost during restoration, the failure explains that manual restoration is needed. The test deliberately has no infinite reconnect behavior.
