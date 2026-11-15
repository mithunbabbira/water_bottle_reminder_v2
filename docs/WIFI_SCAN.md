# Wi-Fi discovery addition (protocol v2, additive)

`{"id":123,"cmd":"wifi.scan"}` starts an asynchronous **ESP32-side 2.4 GHz** scan. Positive ACK means scan started. Completion is a single newline JSON frame:

`{"type":"wifi.scan","id":123,"ok":true,"networks":[{"ssid":"Office Wi-Fi","rssi":-54,"secured":true}],"truncated":false,"hidden_count":0}`

On failure: `{"type":"wifi.scan","id":123,"ok":false,"error":"...","networks":[]}`. May arrive before/after ACK; completion needs both correlated ACK + result. Reject a concurrent scan or scan while Wi-Fi connection/NTP is running with a negative ACK. Deadline 15 seconds firmware, 25 seconds app. Max 15 unique visible SSIDs, strongest first, duplicate SSIDs use their strongest signal result. Empty/hidden names excluded and counted; hidden network manual entry remains available. `truncated` means additional results were omitted. Scanning does not change saved credentials, start NTP or log passwords. Wi-Fi shuts off after scan. Any status `wifi_state` still describes internet sync, not scanning. Optional status `wifi_scan_state` reports idle/scanning/complete/failed.

App: Search networks / searching spinner / retry / visible scan error / empty result help. List selectable network names, signal strength and password-required/open. Selecting fills the exact SSID and clears any previous password when network changes; manual SSID entry is hidden under an explicitly labelled hidden/manual network option. Scan selection does not save/connect. User enters password and explicitly saves. Do not auto-connect or auto-save. Warn 5 GHz-only networks cannot be used. Do not log scan names or full packets unnecessarily.
