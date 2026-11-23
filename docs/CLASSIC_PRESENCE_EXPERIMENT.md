# Standalone Classic Bluetooth presence experiment

## Why this test exists

The old Pi's `pi_controller/bluetooth_presence.py` tries Linux BlueZ `l2ping -c 1 -t 2`, then `hcitool name`, then `bluetoothctl info`. It does not require the phone app. The last fallback can accept cached device metadata and should not be copied as fresh reachability proof.

The actual bottle hardware is an original ESP32-WROOM / ESP32-D0WD-V3 with Classic Bluetooth and BLE, not the earlier BLE-only C3. ESP-IDF exposes `esp_bt_gap_read_remote_name()` and asynchronous `ESP_BT_GAP_READ_REMOTE_NAME_EVT` completion. This experiment tests that Classic mechanism, not raw L2CAP echo and not a BLE app connection.

## Isolated sketch

`classic_presence_test/classic_presence_test.ino` advertises a separate BLE lab service concurrently with Classic operation. Serial commands: `scan`, `probe <address>`, `status`, `help`. No target address is hardcoded, and no bottle calibration/settings are saved or reset. Outputs are explicitly kept quiet. The sketch replaces the product runtime temporarily; restore `bottle/` afterward.

- `scan`: ~10-second Classic discovery; phone may need its Bluetooth settings screen open for initial discoverability.
- `probe`: targeted remote-name request, not an inquiry scan. A successful start is NOT presence; wait for a matching successful callback.
- Result timeout: 20 seconds. Late callbacks are not accepted, and a timed-out in-flight request prevents overlap until controller completion/reset.
- Lab discovery prints device names/addresses locally. It is NOT the finished app setup UI.

## Actual Samsung S25 Ultra test (2026-11-15)

User confirmed the phone address already configured in the Pi (ending **6A:E7**).

| Condition | Observed ESP32 result |
| --- | --- |
| Bluetooth on, settings initially visible | successful remote-name reply, 1046 ms |
| Bluetooth on, settings closed, phone locked, no bottle phone app open | three successful replies: 1404 ms, 530 ms, 519 ms |
| Bluetooth completely off in phone settings, phone still nearby | failure/no reply, status 1, 5127 ms, no name |

BLE advertising remained enabled during these Classic tests. Serial opening resets the lab ESP32, so these observations do **not** establish a same-boot cache-rejection guarantee, long-idle reliability, BLE GATT traffic coexistence, or long-duration soak behavior. Those remain acceptance tests for integration.

The iPhone 15 Pro Max is reported by the user to work with the old Pi, but has **not** been physically tested with this ESP32 experiment yet.

## Not yet implemented in the product

- Mac app Classic-device search/selection and persistent Classic target identity (separate from rotating BLE peer addresses).
- Production scheduler's fresh asynchronous Classic probe before both alert types, bounded retry and active-alert rechecks.
- Genuine phone-off/out-of-range and long-locked-phone acceptance tests after integration.
- iPhone hardware validation and cross-device identity/pairing setup.

The installed normal firmware still uses the mandatory app-link/grace policy until the Classic integration is implemented and validated. Do not present the lab's success as completed product behavior.

After testing, the normal `bottle/` firmware was restored. The first restore upload failed partway through; a retry completed and verified the full application hash. A subsequent real status read confirmed firmware 2.0.0/protocol 2, storage ready and scale ready. Bottle NVS/calibration was not erased.
