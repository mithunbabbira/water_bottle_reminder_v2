# Smart water bottle — architecture (phase 1)

> Historical hand-off for the earlier phase-1 working tree. Current runtime uses protocol v2 JSON, queued BLE callbacks, cooperative Wi-Fi/time and sensor operations, and a guided app. See `README.md`, `docs/PROTOCOL.md` and `docs/HARDWARE_TESTING.md` for the current design. The blocking phone-ping and text command descriptions below no longer describe the runtime.

Hand-off doc for firmware + Flutter BLE tester. ESP32-WROOM, no web portal in the runtime path.

## Repo layout

| Path | Role |
|------|------|
| `bottle/` | ESP32 Arduino firmware (sketch `bottle.ino`) |
| `bottle_tester/` | Flutter app (macOS / iOS / Android) — BLE central tester |
| `ARCHITECTURE.md` | This file |

Target board pins are in `bottle/Config.h` (WROOM, not C3).

---

## Firmware modules

### `bottle.ino`
Main loop: poll scale → tick clock → serial CLI → `Hydration.tick` → BLE weight sample notify.  
Shared command parser `processCommand()` serves **Serial and BLE** (same UTF-8 lines).

### `Config.h`
Compile-time constants: HX711 / LED / buzzer pins, drink/missing thresholds (`MISSING_GRAMS` 100, `PRESENT_GRAMS` 108, etc.), quiet RGB, alert delays.  
**Not stored in NVS.**

### `Scale.h` + `src/HX711.*`
HX711 on **DT=GPIO32**, **SCK=GPIO33**.  
`grams = (raw - tare_offset) / cal_factor`. Needs NVS tare + cal before grams exist.

### `Outputs.h`
White LED GPIO25, buzzer GPIO26, RGB GPIO27/14/12 (common-anode, inverted PWM).

### `BottleNvm.h`
Preferences namespace `"bottle"`. Load/save for scale, schedule, phone MAC, Wi‑Fi nets, etc. Schema key `schema` = `BOTTLE_SCHEMA_VER`.

### `ClockService.h`
Local civil time via ESP `time()` / TZ env. Serial/BLE `time` + `tz`, or boot SNTP. Reminders require `clock.valid`.  
**Active window** = `start_min`–`end_min` (notify inside; quiet outside). Overnight windows supported.

### `Hydration.h` (reminder engine)
State machine: present/missing hysteresis → stabilize → drink/refill/no-change → interval check → presence gate → reminder / missing alert.  
DND mutes buzzer only. Outside active window: quiet color, no reminders (missing alert muted).

### `PhonePresence.h`
Bluedroid `BLEClient`: connect to saved MAC (public then random). Returns Allow / Deny / Busy. Used immediately before reminders (no ping spam).

### `BottleBle.h`
GATT **server** (app is central):

| UUID | Role |
|------|------|
| `6b6f7474-6c65-4001-b5a3-000000000001` | Service |
| `…4002…` | **CMD** write (UTF-8 command line) |
| `…4003…` | **EVT** notify (ACKs, `weight …`, `nvs …`, `evt …`) |

Advertise name: `bottle`.

### `BootWifiTime.h`
Boot-only STA + SNTP: try `wifi_s0..s3` in order → NTP → **`WIFI_OFF`**. BLE runs after Wi‑Fi is off (shared radio on classic ESP32).

### Unplugged
`Portal.h` / `WebPage*` — not started in phase-1 `loop`.

---

## NVS key map (`bottle` namespace)

| Key | Meaning |
|-----|---------|
| `schema` | Schema version |
| `tare_offset` | HX711 zero (long) |
| `cal_factor` | raw→grams scale (float) |
| `baseline_weight` | Last settled bottle weight |
| `daily_total` / `last_day` | Drink total + civil day index |
| `ble_mac` / `ble_irk` | Phone identity (IRK later / optional) |
| `dnd` | Buzzer mute |
| `color` | Packed RGB reminder color |
| `start_min` / `end_min` | Active reminder window (minutes from midnight) |
| `interval_min` | Minutes between checks |
| `beep_type` | 0 off / 1 single / 2 double / 3 long |
| `tz_min` | Minutes east of UTC (multiples of 15) |
| `sta_ssid` / `sta_pass` | Primary STA (mirrors first net) |
| `wifi_s0..s3` / `wifi_p0..p3` | Up to 4 STA networks |
| `ap_ssid` / `ap_pass` / `ap_behavior` | Legacy AP fields (portal unused) |

Dump over BLE/serial: command `nvs` → lines `nvs key=value` or `nvs key=-` (absent), then `nvs end`.  
Also reports `missing_grams` / `present_grams` from **Config.h** (not NVS) plus a note line.

---

## BLE / serial command protocol

Same strings on USB serial (115200) and CMD characteristic.

| Command | Effect |
|---------|--------|
| `status` | Human status + compact EVT status line |
| `nvs` | Full NVS dump (chunked EVT notifies) |
| `window HH:MM HH:MM` | Active window → NVS |
| `interval <min>` | Interval → NVS |
| `beep off\|single\|double\|long` | Beep → NVS |
| `color <r> <g> <b>` | Reminder RGB → NVS |
| `dnd 0\|1` | DND → NVS |
| `phone <mac>` / `phone clear` | Presence MAC → NVS |
| `time …` / `tz <min>` | Clock |
| `tare` / `t` | Zero scale → NVS |
| `calibrate <g>` / `c <g>` | Cal with known grams → NVS |
| `probe` | One presence check |
| `wifi list` / `wifi add <ssid> <pass>` / `wifi remove <ssid>` | Multi-net NVS |
| `i <sec>` | Temporary test interval (RAM) |

EVT examples: `weight raw=… g=…`, `evt reminder`, `probe allow`, `color saved …`.

---

## Flutter tester (`bottle_tester/`)

Single Dart UI for **macOS, iOS, Android** (`lib/main.dart` + `lib/bottle_ble_controller.dart`).

- Scan/connect as BLE central (`flutter_blue_plus`, `License.nonprofit` for personal use).
- Live weight from `weight …` notifies.
- LED **palette** (9 swatches including Off → `color 0 0 0`); taps are **optimistic** (UI first, BLE write background / no-response).
- **Read NVS** button → `nvs` dump panel; missing keys flagged.
- Raw command box + schedule/beep/dnd/phone helpers.

### Run

```bash
export PATH="$HOME/flutter/bin:$PATH"
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
cd bottle_tester
flutter run -d macos
# phone when plugged: flutter run -d <deviceId>
```

Flash matching firmware first (`arduino-cli` FQBN `esp32:esp32:esp32`, port `/dev/cu.usbserial-*`).

---

## Multi-Wi‑Fi boot flow

1. Soft/hard boot → `nvm.load()`.
2. If clock invalid and `netCount > 0`: `BootWifiTime::syncAtBoot`  
   - STA mode → try each saved SSID in order (15s join each) → NTP (15s) → disconnect → **`WIFI_OFF`**.
3. Then `BottleBle.begin` + presence + hydration.
4. Runtime is **BLE + serial**; Wi‑Fi is not left on for reminders.  
5. App/serial can still set time without Wi‑Fi (`time` / `tz`).

---

## Presence model

- Bottle is GATT **server** (app connects to bottle).
- Separately, bottle is GATT **client** when probing phone MAC.
- No MAC → presence treated as Allow.
- IRK/RPA: not required for phase 1; `ble_irk` may be empty.

---

## Pins (WROOM)

| GPIO | Function |
|------|----------|
| 32 | HX711 DT |
| 33 | HX711 SCK |
| 25 | White LED |
| 26 | Buzzer |
| 27 / 14 / 12 | RGB R / G / B |

---

## Known gaps / next steps

- Flutter Mac desktop layout is wide (`Row`); phones need a responsive layout if shipping UI beyond tester.
- Android BLE permissions must be declared in the manifest for phone builds.
- `flutter_blue_plus` may log benign “duplicate method response” on macOS — ignore if scan/connect works.
- Product app (GATT-only, no serial) can reuse the same command strings and UUIDs.
