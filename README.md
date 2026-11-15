# Bottle — standalone hydration reminder

An ESP32-powered bottle base that helps people remember to drink while working. A load cell detects lifting, drinks and refills; an RGB LED, white LED and active buzzer provide local reminders. No Raspberry Pi, cloud account or ESP-NOW network is required.

**Current development target: ESP32-WROOM (classic ESP32), not the C3 Super Mini.** The Flutter app replaces the old web portal. This is a prototype being hardened, not a production-certified product.

## First-time setup

1. Power the base and open the app (`bottle_tester/`). Scan for **bottle** and connect.
2. Set your time zone. Use **Search networks** to select a nearby **2.4 GHz** network, then enter its password. A hidden/manual network option remains available. Alternatively, explicitly use the phone/computer clock as a fallback. Saved networks are used for internet time at cold boot. Network attempts are bounded and do not prevent app connections.
3. With the bottle removed, **zero the empty base**. Then place a known weight on it and calibrate. Never zero the base with the drinking bottle in place. Factory calibration is recommended for a sellable product.
4. Choose your active hours and reminder interval (defaults: 10:00–23:00, every 30 minutes). Put the bottle on the base so it can learn a baseline.
5. Choose colours, beep style and the missing-bottle timeout if desired. Settings live on the base, not just in the app.

No reminder sounds until the scale is calibrated, the clock is valid and the active time window is open. DND mutes sound; it does not disable alert lights. Equal start/end times disable reminders. Overnight windows are supported.

## Drinking behavior

- Less than 100 g for three samples means lifted. A lift stops an alert; the missing-bottle timer begins.
- Over 108 g means returned. Wait for the configured settling time (default 2 seconds), then confirm stable fresh readings.
- At least 50 g lighter (configurable) counts as a drink, treating 1 g water as 1 ml. The LED flashes green and the next interval begins.
- At least 100 g heavier counts as a refill, resets the baseline and restarts the interval without adding intake.
- Smaller sips accumulate against the baseline. Returning without drinking resumes an interrupted reminder.
- No reduction at an interval starts a light reminder. The buzzer begins after the configured light-only delay (default 5 seconds).
- A bottle missing for the configured duration (default 3 minutes) gets a separate-colour alert. Returning clears it.
- Quiet hours, disabled presence, stale/faulted scale data and manual scale measurement must never leave a buzzer latched on.

Totals reset on the next valid local day. Weight differences are estimates, not medical or nutritional measurements: pouring water out looks like drinking, and drinking then refilling before replacement can hide intake.

## Presence: important phone limitation

The peer BLE address is automatically saved on app connection and replaced by the next connected app, as requested for this prototype. It is visible in diagnostics. **A stored MAC is not reliable passive phone presence:** iOS/Android rotate private addresses and usually do not accept incoming BLE connections.

Optional presence gating therefore uses the actual app connection, with 60 seconds of disconnect grace and no pretend boot presence. Leave the app connected for this mode. Mobile background execution/reconnection depends on OS policy and needs device-specific testing. Presence gating is **off by default** so closing the app does not silently stop water reminders. It is not evidence that the person is physically at home (a stationary laptop can stay connected).

## Hardware

| Component | ESP32-WROOM GPIO |
| --- | --- |
| HX711 DT / SCK | 32 / 33 |
| Common-anode RGB R / G / B | 27 / 14 / 12 (LOW = lit) |
| White LED | 25 (HIGH = lit) |
| Active buzzer | 26 (HIGH = on) |

Use suitable resistors and driver transistors for loads exceeding GPIO ratings. GPIO12 is a boot strap pin: validate external pull levels and boot reliability on the final PCB. Do not use this pin table for a C3. Early C3 test sketches are retained for reference.

## Build and run

Firmware (Arduino CLI; ESP32 core 3.3.12 and ArduinoJson 7.4.3 used during development):

```sh
arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=huge_app,UploadSpeed=115200 --output-dir .build/esp32 bottle
arduino-cli upload --fqbn esp32:esp32:esp32:PartitionScheme=huge_app,UploadSpeed=115200 --port /dev/cu.usbserial-0001 --input-dir .build/esp32 bottle
```

Use your actual serial port. Close serial monitors before uploading. **Do not erase all flash** when updating: it contains calibration and user settings. The bogde HX711 library is vendored under `bottle/src/` and must not be replaced by a different HX711 API.

App:

```sh
export PATH="$HOME/flutter/bin:$PATH"
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
cd bottle_tester
flutter pub get
flutter test
flutter run -d macos
```

The app is responsive at phone widths, but Android/iOS shipping, signing, permission flows and background BLE behavior require real-device testing. macOS needs Bluetooth permission.

## Architecture and diagnostics

- `bottle/bottle.ino`: single-owner cooperative main loop, JSON command validation, snapshots and diagnostics.
- `bottle/BottleBle.h`: callback-to-loop queues and MTU-safe newline framing. No sensor/flash work in callbacks.
- `bottle/Hydration.h`, `bottle/HydrationEngine.h`: hardware adapter and deterministic state machine.
- `bottle/Scale.h`: fresh raw readings and cooperative tare/calibration sampling.
- `bottle/BottleNvm.h`: validated settings, calibration, networks, baseline and daily totals.
- `bottle/BootWifiTime.h`: bounded asynchronous Wi-Fi/NTP, then radio shutoff for Wi-Fi.
- `bottle/ClockService.h`: fixed-offset time zone and active-window logic. Fixed offsets do not automatically follow daylight saving; resync the zone when traveling/DST changes.
- `bottle/PhonePresence.h`: nonblocking connection/grace policy.
- `bottle_tester/`: guided Flutter companion app and diagnostics.
- `tests/`, `tools/`: host state-machine tests and serial/BLE hardware regression tools.

[Protocol v2](docs/PROTOCOL.md) defines JSON commands over both serial (115200) and BLE. Firmware and app must be updated together; the previous text-only test protocol is incompatible. Diagnostics expose effective values and whether they were saved. Defaults are valid, not failures. Passwords and identity keys are never returned by diagnostics.

[Hardware test guide](docs/HARDWARE_TESTING.md) separates software checks from physical lift/drink/beep verification. See `docs/TEST_RESULTS.md` for the recorded validation of this development pass when available.

## Before selling

This prototype allows a nearby BLE client to connect, change settings and replace the saved peer; it does **not** implement secure ownership. Before public deployment add a physical pairing/reset action, authenticated encrypted provisioning/ownership, flash/NVS encryption as appropriate, firmware signing and a recovery/update path. Test power loss during writes, long-running sensor drift, temperature compensation, strap-pin behavior, electrical limits, enclosure strain, reconnection, accessibility, radio compliance and mobile lifecycle behavior. Do not market it as a medical device or as production-ready based on simulation tests alone.
