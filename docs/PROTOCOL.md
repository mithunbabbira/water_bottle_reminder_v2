# Bottle BLE protocol v2 (implementation contract)

Target: ESP32-WROOM; BLE service/CMD/EVT UUIDs stay as in BottleBle.h.
All messages are UTF-8, newline terminated JSON objects. BLE chunks are at most 20 bytes by default; both ends reassemble through newline (max 4096 bytes). One command in flight; request IDs correlate acknowledgements. Never log passwords. No JSON contains Wi-Fi passwords or identity keys on readback.

Commands: `{ "id": 1, "cmd": "status" }`. Replies include `{ "type":"ack", "id":1, "ok":true }`, or ok:false with `error`. Ack means handled, not that internet or NTP succeeded. Async outcomes arrive in status. Status is sent after each successful mutation and every 2 seconds while connected.

Commands and fields:
- `status`: status snapshot below.
- `settings`: apply optional fields: start_min/end_min (0..1439, equal disables), interval_min (1..240), beep_type (0 off/1 single/2 double/3 long), dnd (bool), color/reminder_color/missing_color (24-bit RGB int), missing_sec (10..3600), beep_delay_sec (0..30), drink_grams (5..500), settle_ms (500..5000), `presence_enabled` is read-only true (attempts to change it are rejected), tz_min (-720..840 multiple of 15). Validate entire request before persisting.
- `wifi.scan`: discover nearby 2.4 GHz networks asynchronously without saving or connecting; correlated ACK and result. See [scan extension](WIFI_SCAN.md).
- `wifi.add`: ssid (1..32 UTF-8 bytes), password (empty or 8..63 bytes). SSIDs and passwords may contain spaces. Up to four networks. Does not automatically discard the oldest: report error when full. Save then start async sync.
- `wifi.remove`: ssid.
- `wifi.sync`: retry saved networks and NTP now. Async, BLE and scale remain responsive; Wi-Fi switches off after a bounded attempt. After a failed sync, firmware also auto-retries with backoff (30s→5min) while the clock remains invalid; Wi-Fi stays OFF between attempts.
- `time.set`: epoch (UTC Unix seconds >=1700000000), optional tz_min. Manual/app-clock fallback, explicitly labelled.
- `tare`: empty base only; explicit user confirmation in app. `calibrate`: grams (positive known reference weight). These operations may briefly sample the scale but never run from BLE callback.
- `nvs`: send `nvs.begin`, one `nvs.entry` per key, then `nvs.end`. Every message uses same request `id`. Entry fields: key, value (JSON primitive; null if no usable value), stored (actual persistence, not default). Password values only "set"/"empty". Optional/legacy fields marked by app; unstored defaults are not faults.
- `phone.clear`: remove saved peer. Phone BLE peer address is captured automatically on app connect and persisted by main loop, not BLE callback. Address is diagnostic, NOT reliable passive presence proof.

Status schema `{ "type":"status", "protocol":2, "firmware":"2.0.0", "ready":true, "clock_valid":true, "time_source":"ntp|app|rtc|unset", "epoch":1790000000, "tz_min":330, "active":true, "weight_g":650.5|null, "raw":123|null, "scale_state":"ready|need_tare|need_cal|fault|no_data", "state":"warming|idle|missing|settling|reminder|missing_alert|paused|scale_error", "baseline_g":700|null, "today_ml":50, "last_drink_ml":50, "next_check_sec":1800, "phone_connected":true, "phone_present":true, "phone_mac":"..."|null, "wifi_state":"idle|connecting|syncing|synced|failed|no_networks", "wifi_error":"" , "wifi_ssid":"...", "settings":{...all settings fields above...}, "networks":[{"ssid":"...","secured":true}] }`.

Event: `{ "type":"event", "name":"drink|refill|lifted|returned|reminder|missing_alert|paused|scale_error", "ml":50 }`. No command IDs required.

Presence: ALWAYS gate both alert types on an active BLE app link (60-second disconnect grace, no boot grace). There is no writable toggle; `presence_enabled:true` is retained only as a read-only compatibility/diagnostic field. Old disabled NVS values are normalized without discarding other settings. UI must explain that closing/backgrounding the app can suppress reminders and that mobile background behavior needs real-device testing. A stored peer MAC alone is not presence. Automatic claim on app connection is prototype behavior, not secure ownership; no production security claim.
