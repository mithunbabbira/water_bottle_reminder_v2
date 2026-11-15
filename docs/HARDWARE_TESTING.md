# Hardware smoke and acceptance testing

Use [PROTOCOL.md](PROTOCOL.md) as the firmware wire contract. These tools test a
**running protocol-v2 firmware**, not the bootloader, flashing procedure, phone UI,
or the accuracy of a physical drink measurement.

## Safety and ownership first

- Expected hardware: **classic ESP32-WROOM, 4 MB flash**, not an ESP32-C3.
  The expected USB device on this bench is `/dev/cu.usbserial-0001`. A successful
  JSON status does **not** identify the silicon or measure flash capacity; retain
  the board identification from the separate, authorized backup/flash procedure.
- **Do not open the serial device or connect BLE while another session is backing
  up/flashing.** Wait for the operator's explicit all-clear. Close serial monitors,
  the phone app, and other clients. Never run the serial and BLE suites together.
  Serial responses can otherwise be coupled to a BLE TX queue/subscription.
- Nothing in these instructions should be run as an automatic post-flash hook.
  The commands below are for the operator to run after exclusive ownership is
  handed over. The helpers do not flash, launch the app, or intentionally reset.
- Opening a USB serial device can still glitch DTR/RTS and reset some adapters.
  The serial helper deasserts both lines **before** open and uses an exclusive
  POSIX open. This reduces, but cannot eliminate, driver-induced reset. The
  default two-second startup wait tolerates boot text; it is not a reset command.
- Leave the bottle and base stationary throughout automated tests. Keep the
  mains/USB setup dry. Normal firmware timekeeping, baseline initialization,
  date rollover, and real sensor processing continue while the tests run; this
  is not a freeze of all NVS writes. Avoid the local date boundary when comparing
  totals. Already configured alerts may still sound.
- No automated test sends `tare`, `calibrate`, a daily reset, `time.set`,
  `phone.clear`, `wifi.sync`, or any synthetic weight/drink event. No real Wi-Fi
  password is accepted as a command-line argument or printed. Do not attach raw
  terminal captures containing credentials to bug reports.

## Environment and offline checks

Python **3.10+** is required. Both `--help` and the unit tests are offline: they do
not import pyserial/bleak, scan, or connect. From the repository root:

```sh
python3 -m unittest discover -s tools -p 'test_*.py' -v
python3 tools/serial_smoke.py --help
python3 tools/ble_smoke.py --help
```

If the existing Python already has pyserial, it can run the serial suite directly.
Do **not** install packages into system Python. For an isolated, reproducible
hardware environment (including optional BLE):

```sh
python3 -m venv tools/.venv-hardware
tools/.venv-hardware/bin/python -m pip install -r tools/requirements-hardware.txt
```

`tools/requirements-hardware.txt` explicitly pins pyserial and bleak. The BLE
package is not needed by `serial_smoke.py`. Do not commit the venv, generated
artifacts, or Python caches. This procedure deliberately does not install anything
or acquire hardware until the operator runs it.

## Automated serial smoke

**Only after the backup/flash owner releases the port**, run:

```sh
python3 tools/serial_smoke.py \
  --port /dev/cu.usbserial-0001 \
  --output tools/artifacts/serial-smoke.json
```

Or substitute `tools/.venv-hardware/bin/python` for `python3`. The port is required;
there is no auto-detect or silent fallback to another USB device. Baud defaults
to 115200. The first run should omit `--exercise`.

The default suite intends **no successful settings/network writes**:

1. Require `status`, a matching integer-ID `ack` with `ok:true`, `protocol:2`,
   ready storage, correctly typed settings, and up to four network summaries.
   An uncalibrated scale (`need_tare`, `need_cal`) does not fail a protocol test.
2. Collect the **entire** ID-correlated `nvs.begin` / `nvs.entry` / `nvs.end`
   stream, not just its ACK or first entry. Require all documented core setting,
   calibration, hydration, peer, and four network-slot fields, unique keys,
   primitive values, and boolean `stored` flags. Extra fields are retained.
   `sta_ssid`/`sta_pass` primary-network aliases may be absent.
3. Require password/IRK readback to be only `set`, `empty`, or null; an exposed
   credential fails the test **without printing it**. Output is independently
   sanitized again, including NVS `{key, value}` envelopes. Peer MACs are also
   redacted. SSIDs, weights, settings, and totals remain diagnostic information;
   review them before sharing the artifact. Raw device error strings and boot
   logs are never stored or printed.
4. Wait, with a deadline, for any **already running** boot Wi-Fi attempt to end.
   This avoids mistaking a busy rejection for password validation. No sync is
   started by the helper. The default 90-second wait accommodates four bounded
   connection attempts plus NTP. Failed/no-network time sync is not a protocol
   failure; physical time-source checks are separate.
5. Submit invalid settings types, explicit nulls, numeric bounds, a fractional
   integer, numeric boolean, and a non-quarter-hour timezone. Each request also
   contains a valid **changed** DND or color field. Require a negative ACK and
   an unchanged full settings/network snapshot after **each** request, proving
   whole-request rejection rather than partial application. Omission is optional;
   explicit null is tested as a wrong type, not an omitted field.
6. Try short, overlong, and non-string Wi-Fi passwords against an SSID proven
   absent in the initial snapshot. Removing that unknown SSID must fail safely.
   Existing credentials are never overwritten, read in clear, or deliberately
   removed. This does not test correct-password connection, network capacity, or
   open-network provisioning because those would require successful writes.
7. Require negative ACKs for an unknown command, malformed JSON, and a frame
   larger than 4096 bytes. Unparseable/overflow requests use ACK ID `0` because a
   trustworthy command ID is unavailable. Require status responsiveness afterward.
8. Send a JSON object in two fragments, withholding its newline, verify neither
   fragment has caused execution, then finish the frame and require ACK plus
   status. Send two newline-delimited
   status commands back-to-back and require both ACKs/responses. This deliberate
   framing test is the only two-command pipeline.
9. Stream NVS again and compare persisted settings, credential markers/SSIDs,
   calibration, and `stored` flags before/after. Hydration baselines/totals/date
   fields are excluded from this comparison because the real device owns their
   autonomous updates. Finish with a fresh responsive status.

Reads, writes, input buffers, message queues, and entire NVS streams are bounded.
Boot text, partial UTF-8, coalesced lines, periodic status, and unrelated event
frames are handled without assuming a transport read is a JSON message. Request
IDs increase monotonically. **Status currently has no request ID**: the helper
accepts a status only after the matching command ACK on an exclusive connection.
An ACK alone is not counted as a status or completed NVS response. A Wi-Fi ACK is
not proof of Internet/NTP success.

Defaults: `--timeout 12` seconds per command, `--stream-timeout 45` for the complete
NVS stream, `--wifi-wait 90`, `--startup-wait 2` (serial only). Large NVS dumps are
emitted one entry per firmware loop. With no BLE client, serial entries can drain
almost immediately; over BLE they wait for paced notification output. There are
no arbitrary sleeps between NVS entries and no assumption that they take two
seconds each. Increase explicit deadlines on a slow host rather than removing
them. A full BLE run can take minutes because every rejection includes a status.

Exit status is **0 only on success**, nonzero on validation, transport, timeout,
artifact-write, or cleanup failure (130 on interruption when possible). The JSON
artifact includes per-test results/timings, sanitized status/NVS snapshots,
decoder counters, and restoration outcome. A passing offline fake-device test is
not a passing physical-device test. Failed tests stop subsequent probes to limit
side effects. Inspect the first failure, not just the final count.

### Optional reversible settings exercise

```sh
python3 tools/serial_smoke.py \
  --port /dev/cu.usbserial-0001 \
  --exercise \
  --output tools/artifacts/serial-exercise.json
```

This adds one successful `settings` write changing only DND, beep type, and idle
color. It validates readback, then restores changed fields from the initial
snapshot in `finally`, including on failure/interruption when the connection
still works. The initial/final NVS comparisons occur **before** this deliberate
exercise; `final_status` is refreshed after restoration. This verifies settings
round-trip, **not** the audible pattern or emitted light.

There is **no settings dry-run**: do not invent or send a `dryrun` flag, which an
implementation might ignore while saving settings. Exercise writes consume flash
cycles; do not put it in a rapid loop. Restoring values does not erase a newly
created settings blob or revert `stored:false` to false, nor reconstruct timer
history. Thus settings values are reversible, not the exact flash-write history.

Defensive cleanup is also enabled for the default invalid-settings tests: if
buggy firmware partially accepts them, the helper attempts to restore changed
settings from the snapshot. If it unexpectedly adds the originally absent test
SSID, cleanup removes **only that SSID**, after a bounded Wi-Fi-idle wait. A failure
remains a failure after rollback. Original password bytes are never available to
the helper, so it cannot repair arbitrary firmware credential corruption. Power
loss, a killed process, transport loss, or broken validation can prevent cleanup;
use the sanitized initial settings snapshot for operator recovery. No flash erase
or backup restore is attempted automatically.

## Optional BLE transport smoke

Use the dedicated venv above; do not install bleak into system Python. Grant the
terminal/Python Bluetooth permission on macOS. Obtain the explicit peripheral
identifier through the operating system or the existing app's device picker
outside this test; the helper does not guess between devices. On macOS it is
generally a CoreBluetooth UUID, **not** the ESP32 MAC address.

**Important side effect:** current prototype firmware automatically saves the
connecting BLE peer address. A test connection may overwrite the previously saved
phone MAC. The protocol has no restore-peer operation, so this cannot be undone by
the helper. Prefer serial for a no-peer-change test. BLE requires explicit
`--allow-peer-update`; use only on a test unit or after the owner authorizes that
side effect. Do not use `phone.clear` as a supposed rollback. Reconnecting the
intended phone may claim the diagnostic peer again, but this is a separate,
user-controlled action, not a security guarantee.

```sh
tools/.venv-hardware/bin/python tools/ble_smoke.py \
  --address 'EXPLICIT-PERIPHERAL-UUID' \
  --allow-peer-update \
  --output tools/artifacts/ble-smoke.json
```

The same negative/framing/NVS suite runs over:

| Purpose | UUID |
| --- | --- |
| Service | `6b6f7474-6c65-4001-b5a3-000000000001` |
| Command write | `6b6f7474-6c65-4002-b5a3-000000000001` |
| Event notify | `6b6f7474-6c65-4003-b5a3-000000000001` |

Notifications are subscribed **before commands**. Every GATT write uses
`response=True`, awaited sequentially, with **at most 20 bytes per fragment** to
exercise ATT MTU 23 even if a larger MTU is negotiated. Incoming notifications are
queued with a fixed bound and reassembled through newline; a GATT write response
is **not** the JSON command ACK. Disconnects, notification overflow, missing ACKs,
and incomplete NVS streams fail with deadlines. A later run/reconnect uses a new
transport/decoder; it does not reuse partial frames from an old link. The suite
does not prove all mobile background/reconnect behavior; inspect that separately.

## Physical acceptance — operator observation, not automated claims

Use a preconfigured, correctly calibrated unit. Tare/calibration are **separate,
explicit maintenance operations** requiring consent, an empty base for tare, and
a known reference mass for calibration. They change calibration and hydration
baseline state and are intentionally absent from the smoke tools. Do not use a
loaded bottle as the empty-base reference.

Record firmware/version, hardware ID, calibration status, starting settings,
weight/baseline, daily total, time source, and observed event sequence. Use a
separate physical-test day/unit if you must preserve today's drinking history:
real drinks intentionally change daily totals and are not undone by this suite.
Do not fake a drink or reset daily totals to make a demonstration pass.

| Check | Operator procedure and expected observation |
| --- | --- |
| Safe boot / no implicit tare | Before a separately authorized restart, note `tare_offset`/`cal_factor` and the loaded bottle weight. Reboot with the bottle still on the base, let readings settle, and read them again. Offset/factor must not be replaced by a loaded-bottle zero; loaded mass should remain meaningful. No boot tare, auto-calibration, or invented drink should occur. |
| Missing calibration / defaults | On a **separate fresh unit**, expect `need_tare` or `need_cal`, null grams where unavailable, and safe/silent alert behavior. Never erase an existing unit just to run this test. `stored:false` plus a usable default is normal, not corruption. Optional/legacy Wi-Fi, AP, MAC/IRK, baseline, schema, or absent slot fields need not be stored. |
| Stable idle | Place the configured bottle on a level, vibration-free base and let settling complete. Confirm stable plausible grams/baseline, no repeated drink credits while motionless, and appropriate idle color. |
| Lift / unchanged return | Lift the bottle fully, without drinking, then return it unchanged. A real lift must be recognized; alerts should stop while lifted. Wait for the configured settling/confirmation, and check no drink credit is manufactured by transient readings. |
| Real drink | Lift, drink a measured quantity above the configured drink threshold, and return. Wait for settling/confirmation. Confirm exactly one drink event and corresponding baseline/daily-total update, approximately 1 g per mL for water. Continued stillness must not count it again. Compare against a reference scale, not just two JSON fields. |
| Refill | Lift, add water above the refill threshold, return, and let readings settle. Baseline should move appropriately without adding refill volume as consumed water. |
| Reminder timing and lift silence | At valid local time inside the active window, with DND off and presence rules satisfied, wait for the configured interval. Observe reminder color/white output and the configured beep delay. Physically lift to stop the alert promptly; do not count writing `beep_type` as this check. |
| Missing-bottle alert | With an applicable active window, leave the bottle off longer than configured `missing_sec`. Observe the separate missing color/pattern; return it and verify stable recovery, without a false drink. |
| DND, window, presence | With consent, record and temporarily change settings, then restore them. Check DND suppression, start=end disabled window, cross-midnight behavior, and presence-enabled active BLE/grace behavior. A stored MAC alone is not passive presence. Default presence gating is disabled; mobile background disconnection is a real limitation. |
| Wi-Fi/time | In a separate authorized provisioning test, use an actual 2.4 GHz network. ACK only means request handled. Observe connecting/syncing ending with `time_source:ntp` or a bounded failure while scale and BLE remain responsive. App-set/RTC/unset time must be labelled accurately. Do not put real passwords into shell history or reports. Restore network changes manually only if you possess the original credentials. |
| Persistence | After an operator-authorized reboot, recheck settings, calibration, baseline and daily total, allowing legitimate local-day rollover. Serial smoke compares current-session persistence diagnostics; it does not prove reboot survival. Do not restore an old baseline/totals snapshot over real drinks. |
| Sensor failure | Only on a safe bench, disconnect power before changing load-cell wiring. An unreadable/railed sensor must report error/no-data and must not beep indefinitely or create drinks. Restore wiring before normal use. No automated tool disconnects hardware. |

### Acoustic and electrical behavior needs human inspection

The hardware uses a common-anode RGB LED (inverted channel drive), white LED on
GPIO25, buzzer on GPIO26, RGB GPIO27/14/12, HX711 DT32/SCK33. Verify the actual
board/wiring, LED polarity, brightness, and pin mapping before interpreting UI
colors. This is **not** the C3 pin map.

`beep_type` is off/single/double/long (0/1/2/3). While an audible alert is active,
verify 0 is silent; 1 produces single short chirps; 2 produces two short chirps
with a quiet gap; 3 is a long/continuous tone until silenced. Current output timing
uses an approximately 520 ms repeating cycle, with 80 ms chirps and the second
double chirp around 150–230 ms. The electrical on/off envelope is not proof of an
audible tone: an active buzzer and a passive piezo behave differently under DC.
An operator must **listen** and, where necessary, inspect GPIO26 with a suitable
scope/logic analyzer. Do not claim acoustic acceptance from a successful ACK,
settings readback, or a Python unit test. Check lifting and DND silence with the
real transducer, not just a status label.

## Interpreting defaults and failures

Typical unpersisted defaults are DND off, idle RGB `0x8000FF`, active window
10:00–23:00, interval 30 min, double beep, UTC+05:30, missing timeout 180 s,
beep delay 5 s, drink threshold 50 g, settling 2000 ms, reminder `0x00FFFF`,
missing `0xFF0000`, and presence gating off. The running firmware/status is the
authoritative configured value. A brand-new device need not write defaults to
NVS; legacy migration may create a validated settings blob without erasing old
keys. `stored` reports persistence, not whether a default works.

A type/null/range rejection failure is a firmware/contract mismatch, not a reason
to retry the same request with `dryrun`. Storage-not-ready, timeout, missing end
frame, leaked secret marker, post-error unresponsiveness, and cleanup failure are
real smoke failures. Preserve only the sanitized artifact; do not paste raw
library exception strings or credential-bearing commands into an issue. Report
which tests were **actually run on hardware**, versus offline helper tests and
physical checks still awaiting the operator.
