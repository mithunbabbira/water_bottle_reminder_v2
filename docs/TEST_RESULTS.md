# Validation log — 2026-11-15

This is a development validation record, not a production certification.

## Completed by the main session

- Device identification: classic ESP32-D0WD-V3 revision 3.1, 4 MB flash. Existing partition layout matches `huge_app` (NVS 0x9000/0x5000, app 0x10000/0x300000).
- Backed up partition table, NVS and OTA metadata (0x8000–0xffff) to the session's approved temporary directory, mode 0600. Full-flash read attempts failed with serial corruption; **no complete firmware backup was obtained**. No erase-all operation used.
- Existing firmware reports tare and calibration present, clock unset. Live load-cell samples were observed; this does not validate calibration accuracy.
- macOS Bluetooth discovery finds `bottle`.
- `python3 -m unittest discover -s tools -p 'test_*.py' -v`: **27 passed** (offline smoke-tool regression tests, not physical bottle tests).
- Host C++ `tests/json_command_test.cpp` against installed ArduinoJson 7.4.3: **14 cases passed** for single-document boundaries, spaces, escaped strings and embedded NUL rejection.
- `git diff --check`: clean at this stage.

## Integrated firmware verification

- `bash tests/native/run.sh`: **19 core test groups passed**, under AddressSanitizer and UndefinedBehaviorSanitizer. Includes hydration transitions, distinct/stale readings, moving-water retry, rollover, time zones, NVS migration/failure/atomic peer clear and adapter persistence.
- Firmware compiles for WROOM `huge_app`: approximately **1.71 MB flash (54%)**, **76,972 bytes static RAM (23%)**.
- Firmware uploaded to the actual ESP32 at 115200 baud; esptool verified all written hashes. NVS range 0x9000–0xdfff was not erased.
- Real serial smoke suite with reversible DND/beep/colour exercise: **74 passed, 0 failed**. Settings restoration succeeded. Saved tare/calibration remained available. Sanitized artifact: `tools/artifacts/serial-smoke.json` (locally ignored).
- Real runtime initially reports scale ready, ~24 g with the base unloaded, clock invalid, baseline not yet learned, ~88 KB free heap. Calibration accuracy is not inferred from this reading.

## Pending at this checkpoint

- Real BLE smoke suite (minimum 20-byte fragments), including reversible settings writes/restoration: **74 passed, 0 failed** on the retry. The first run read status/NVS but timed out on a subsequent write; an old Bottle Tester process was still running and was subsequently terminated. Interference is a possible explanation, not a proven root cause. Retain both artifacts: `tools/artifacts/ble-smoke.json` and `tools/artifacts/ble-smoke-retry.json`.
- Flutter main-session rerun: `flutter analyze` **no issues**; `flutter test --reporter expanded` **45 passed**. Includes narrow/desktop/large-text layout, guarded tare/calibration, Wi-Fi spaces/validation, ACK failure/timeout, default diagnostics, cleanup and reconnect behavior.
- Filled bottle physically placed by the user: raw scale response corresponds to ~1.63 kg; baseline learned and persisted. Accuracy still requires a reference mass.

## Native app and physical acceptance

- Final firmware build uploaded and verified: **1,707,520 bytes flash**, **76,972 bytes static RAM**. Calibration was not re-zeroed or replaced.
- macOS debug build passed. Opt-in native Flutter integration test against the actual bottle **passed**, including status/NVS, negative ACK, DND mutation and restoration. Test launcher reported a foregrounding warning, but the native test executed and completed successfully.
- Final app unit/widget rerun after clearer DND labeling, timezone dropdown and visible storage-error reporting: **45 passed**; final analyzer: **no issues**.
- User physically confirmed light-first/delayed sound, stopping on lift, reminder resumption after a no-drink return, green confirmation after a drink, red missing-bottle alert after extended absence, and clearing the missing alert on return.
- Live BLE readback confirmed **72.30994 ml** recorded, baseline **1564.713 g**, current weight **1564.561 g**, no storage fault. The subsequent cyan alarm was correctly `reminder`, not `missing_alert`, with the user's pre-existing **1-minute** interval.
- Physical USB logging session lost a response at approximately 33 seconds and restored its temporary settings in `finally`. Therefore no claim of complete continuous timing telemetry is made. A follow-up accelerated BLE test correctly aborted before changing settings because the bottle was off the base. The user's observations above are separately identified as manual acceptance evidence.
- Original settings preserved: interval **1 minute**, active window **00:00–23:59**, missing timeout **180 seconds**. User wants schedule selection in the app; no replacement everyday schedule was imposed. App clock was explicitly set for the physical test; this is not proof of internet/NTP success.

## Wi-Fi search extension

- ESP32-side asynchronous discovery added, with bounded result count, strongest-first deduplication, security/signal metadata and no credential persistence.
- `bash tests/wifi_scan/run.sh`: **6 scan test groups passed** under ASan/UBSan (async progress, deduplication/security, top-15 cap, empty result, failure/timeout, rollover).
- Existing `bash tests/native/run.sh`: **19 groups passed** again after the addition.
- Firmware builds: **1,710,576 bytes flash**, **77,372 bytes static RAM**.
- Updated firmware flashed to the real ESP32 with verified hashes; NVS preserved.
- Real Bluetooth scan smoke **passed**: found **9 nearby networks**, validated deduplication, strongest-first ordering, result size and security/signal types. Status remained responsive, scale reported ready, saved networks and settings were unchanged. No password or SSID was printed by the smoke summary.
- App tests after search/selection: **63 passed**, analyzer **no issues**. Includes ACK/result order, scan timeout/retry, stale IDs, exact SSID selection, password clearing, hidden/manual entry and narrow/large-text layouts.
- A subsequent BLE regression reproduced a write timeout after NVS read (so earlier old-app interference was not proven causal). Added explicit Bluedroid congestion/send-credit gating and reduced notification pacing. Updated firmware uploaded with verified hashes (**1,710,744 bytes flash**, **77,372 bytes static RAM**). A real-BLE repeated-read stress check then completed **three consecutive status → full NVS → status rounds**, with all 36 keys and successful writes after each dump, in 18.2 seconds. This is a bounded regression check, not proof that every Bluetooth failure is eliminated.
- Extended real native Flutter → BLE → ESP32 integration **passed** in 29 seconds: full diagnostics, real Wi-Fi discovery, result validation, unchanged saved credential metadata, follow-up commands and disconnect. Native macOS build passed (foregrounding warning remains).
- Final UI tests **63 passed** after also requiring a password for a selected secured network. No credentials were entered or modified by scan validation.

## Still not verified / release limitations

- Real Wi-Fi credentials + successful internet NTP and a full cold-power-cycle sync must still be checked through the Mac app. Credentials should be entered in the app, not chat or serial logs.
- Exact physical missing timeout, refill behavior with a measured added volume, independent reference-mass calibration accuracy, power-loss writes and multi-day soak remain unverified on hardware (host tests cover corresponding logic where applicable).
- Android/iOS builds, permissions and background BLE are not hardware-tested. Passive MAC ping is not used as false presence proof.
- Time zones are fixed UTC offsets, not automatic DST/IANA rules. Secure physical ownership/provisioning and commercial plugin licensing remain release gates; see RELEASE_CHECKLIST.md.

