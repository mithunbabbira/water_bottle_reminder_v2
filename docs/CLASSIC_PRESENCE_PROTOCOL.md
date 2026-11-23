# Classic presence integration contract (protocol v2 additive, firmware 2.1.0)

Presence is mandatory. BLE connection is CONFIGURATION TRANSPORT ONLY, never proof of owner presence. A selected Bluetooth Classic device is separately stored atomically in NVM; raw BLE peer MAC remains diagnostic. No selected Classic target => no drink/missing alerts, actionable setup status. No manual MAC entry required in normal UI.

## Commands

- `phone.scan` no args: positive ACK means started. Classic general inquiry ~10 seconds; phone may need Bluetooth settings open once to be discoverable. Async result: `{type:"phone.scan",id:123,ok:true,devices:[{address:"AA:BB:CC:DD:EE:FF",name:"Galaxy S25 Ultra",rssi:-50}],truncated:false}`; errors ok:false,error. Max 8 unique entries with <=48-byte UTF-8-safe names, sorted signal, unnamed entries allowed but clearly labeled. Result timeout 20 sec firmware, 30sec app. Must reject busy state (Classic probe, discovery, Wi-Fi join/scan) with negative ACK. Result identity bound to initiating BLE session; cannot select from an older/other session.
- `phone.select` fields address (exact discovered address), scan_id (completed scan request id): explicit confirmation in app, then firmware validates address belongs to that still-current discovery for this session, derives display name from result, writes ONE atomic Classic target record. Do not accept untrusted arbitrary names or auto-select on scan/connect. Starts an automatic bounded probe after saving; ACK reflects persistence, not presence. Send status after mutation. Selecting/replacing clears cached reachability. UI refreshes readback and shows selected target/checking/reachable/not reachable. Re-selection/replacement must not inherit previous target's presence.
- `phone.probe`: positive ACK then `{type:"phone.probe",id:124,ok:true,present:true|false,reason:"reply|no_reply|timeout|radio_error",elapsed_ms:1000}`. Requires selected target, rejects busy; 20sec firmware timeout,30sec app. API ESP_OK only means started; only successful matching completion is positive. No cached-name fallback. Manual user probe may briefly show result; alerts still require a NEW fresh probe at the due decision, not a previous manual result.
- `phone.clear`: clears CLASSIC target atomically (not calibration/networks), cancels/invalidates reachability and silences alerts on next tick. Existing BLE peer may remain diagnostic. Old aliases should not resurrect it on reboot.

## Status additions

`phone_connected` still means app BLE link (backward-compatible name).
`app_peer_mac`: raw last BLE peer address diagnostic; never use it for Classic probing.
`phone_mac`: selected Classic address or null.
`phone_name`: selected friendly name or null.
`phone_present`: current fresh verified Classic reachability, NEVER app link.
`presence_method`: "classic_remote_name".
`presence_state`: "not_configured|unknown|checking|present|away|timeout|radio_error|scanning|stalled".
`presence_error`: safe actionable error string.
`presence_last_check_age_sec`: elapsed since last completed probe or null.
`phone_scan_state`: idle/scanning/complete/failed.
`settings.presence_enabled`: read-only true retained for diagnostics; no toggle.
NVS logical entries `classic_mac`, `classic_name` with stored flags; `ble_mac` remains app peer. Old BLE identities MUST NOT be migrated into Classic target.

## Automatic probing / alarm gate

A clock-valid, scale-healthy, active-window drink reminder due or missing timeout requests a fresh asynchronous Classic probe BEFORE starting either alert. Manual positive probe must not pre-authorize a later notification. Maintain a due/decision token or episode so automatic success applies to that alert episode only. Recheck every 15 seconds while alerting; failed/expired/unknown probe suppresses/cancels alerts. Away retries every15sec when an alert remains due, never a tight loop. No target, app disconnected, radio/scan busy, time unset, no scale => no blocking calls and no fake 'here'. App connection is not required at runtime. Selected target stays in NVM over reboot; boot always starts unknown and needs a probe.

Fresh positive auto result expires after30sec maximum; failure clears positive immediately. Late/wrong-target callbacks cannot authorize newer probes. If a name request times out and cannot be canceled, quarantine until completion or radio reset; never overlap ambiguous same-address requests. Discovery may temporarily suppress alerts; no network/flash/sensor operations inside BT callbacks. Queue bounded copies into loop.

## Persistence API for integration

BottleNvmData: `bool hasClassicPhone`, `uint8_t classicMac[6]`, `char classicName[49]`.
BottleNvm: `bool saveClassicPhone(const uint8_t mac[6],const char *name)`, `bool clearClassicPhone()`; one atomic versioned CRC blob, separate from BLE peer. Retain settings/calibration/totals/networks. Native tests for failure/migration/clear.

## Driver API (backend/main coordination)

Class `ClassicPresence` in `bottle/ClassicPresence.h`:
- `bool begin()` after BottleBle initialized; `void setTarget(const uint8_t *mac)` (nullptr clears; invalidates in-flight results); `void tick(uint32_t now)`
- `bool busy() const`; `bool startScan(int id,bool fromBle,uint32_t session)`; `bool takeScanFinished()`; getters `scanId()`, `scanFromBle()`, `scanSession()`, `scanCount()`, `scanTruncated()`, `scanError()`, `scanState()`, `device(i)` struct address[6],name[49],rssi.
- `bool startProbe(int id,bool fromBle,uint32_t session,bool automatic=false)`; `bool takeProbeFinished()`; getters probeId/fromBle/session, probePresent(),probeReason(),probeElapsedMs(). Auto id=0 and no response packet needed; automatic result still delivered to orchestrator policy.
- `const char *state()`, `const char *error()`, `bool here(uint32_t now)`, `bool hasLastCheck()`, `uint32_t lastCheckAgeSeconds(uint32_t now)`.
- Failed begin returnsfalse, no lie about radio initialized. All loop APIs nonblocking.

Hydration: `bool needsPresence(uint32_t now) const` surfaces a due/active reminder/missing episode only when healthy/clock active; existing tick accepts bool phoneHere. Main coordinates a separate fresh episode authorization (manual results never authorize). Driver identity changes while an in-flight callback pending invalidate it without unsafe overlap.

Main owns bottle.ino and integrates these APIs; backend agent owns driver/NVM/engine/native tests. App agent owns only bottle_tester.
