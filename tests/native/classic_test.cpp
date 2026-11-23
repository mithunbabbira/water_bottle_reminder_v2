#include "ClassicPresence.h"
#include "BottleNvm.h"
#include <iostream>
#include <stdexcept>
#include <thread>
#include <functional>
#include <string>
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(__func__) + ":" + std::to_string(__LINE__) + " " #x); } while (0)
static ClassicPresence p;
static const uint8_t a[6] = {0x20,1,2,3,4,5}, b[6] = {0x30,1,2,3,4,5};
static bool is(const char *s, const char *v) { return strcmp(s, v) == 0; }
static void at(uint32_t now) { NativeArduino::now() = now; }
static void tick(uint32_t dt = 0) { NativeArduino::now() += dt; p.tick(millis()); }
static void reply(const uint8_t *mac, bool success = true) {
  esp_bt_gap_cb_param_t param = {}; memcpy(param.read_rmt_name.bda, mac, 6);
  param.read_rmt_name.stat = success ? ESP_BT_STATUS_SUCCESS : ESP_BT_STATUS_FAIL;
  NativeBt::state().callback(ESP_BT_GAP_READ_REMOTE_NAME_EVT, &param);
}
static void stop() {
  esp_bt_gap_cb_param_t param = {}; param.disc_st_chg.state = ESP_BT_GAP_DISCOVERY_STOPPED;
  NativeBt::state().callback(ESP_BT_GAP_DISC_STATE_CHANGED_EVT, &param);
}
static void found(int id, int8_t rssi, const char *name) {
  esp_bt_gap_cb_param_t param = {}; param.disc_res.bda[0] = 0x40; param.disc_res.bda[5] = id;
  esp_bt_gap_dev_prop_t props[] = {{ESP_BT_GAP_DEV_PROP_RSSI, 1, &rssi},
    {ESP_BT_GAP_DEV_PROP_BDNAME, static_cast<int>(strlen(name)), const_cast<char *>(name)}};
  param.disc_res.num_prop = 2; param.disc_res.prop = props;
  NativeBt::state().callback(ESP_BT_GAP_DISC_RES_EVT, &param);
}
static void initialization() {
  CHECK(!p.here(0)); CHECK(is(p.state(), "not_configured")); CHECK(!p.hasLastCheck());
  p.tick(0); CHECK(!p.startScan(1, true, 1)); CHECK(!p.startProbe(1, true, 1)); CHECK(!p.begin());
  NativeBt::controller() = true; CHECK(!p.begin()); NativeBt::bluedroid() = true;
  NativeQueues::fail() = true; CHECK(!p.begin()); NativeQueues::fail() = false;
  NativeBt::state().registration = -1; CHECK(!p.begin()); NativeBt::state().registration = 0;
  CHECK(p.begin()); CHECK(p.begin()); CHECK(!p.startProbe(1, true, 1));
}
static void asynchronousIdentityAndExpiry() {
  at(1000); p.setTarget(a); CHECK(is(p.state(), "unknown"));
  CHECK(p.startProbe(24, true, 71)); CHECK(!p.startScan(1, true, 1)); CHECK(!p.startProbe(25, true, 71));
  CHECK(!p.here(millis()) && is(p.state(), "checking")); CHECK(!p.takeProbeFinished());
  CHECK(!memcmp(NativeBt::state().probed, a, 6));
  reply(b); tick(100); CHECK(p.busy()); CHECK(!p.takeProbeFinished());
  // Callback thread cannot update loop state; only tick accepts the completion.
  std::thread t([] { reply(a); }); t.join(); CHECK(!p.here(millis()));
  tick(900); CHECK(!p.busy()); CHECK(p.takeProbeFinished()); CHECK(!p.takeProbeFinished());
  CHECK(p.probeId() == 24 && p.probeFromBle() && p.probeSession() == 71 && !p.probeAutomatic());
  CHECK(p.probePresent() && is(p.probeReason(), "reply") && p.probeElapsedMs() == 1000);
  CHECK(p.here(millis()) && p.hasLastCheck() && p.lastCheckAgeSeconds(millis()) == 0);
  CHECK(p.here(millis() + 29999)); CHECK(!p.here(millis() + 30000));
  tick(30000); CHECK(is(p.state(), "unknown"));
  CHECK(p.startProbe(0, false, 0, true)); CHECK(!p.here(millis())); CHECK(p.probeAutomatic());
  reply(a, false); tick(500); CHECK(p.takeProbeFinished() && !p.probePresent());
  CHECK(is(p.state(), "away") && is(p.probeReason(), "no_reply"));
}
static void replacementAndClearQuarantine() {
  p.setTarget(a); CHECK(p.startProbe(41, true, 2)); p.setTarget(b);
  CHECK(!p.here(millis()) && !p.hasLastCheck()); CHECK(p.busy());
  CHECK(!p.startProbe(42, true, 2)); CHECK(!p.startScan(42, true, 2));
  reply(b); tick(100); CHECK(p.busy()); reply(a); tick(500); CHECK(!p.busy());
  CHECK(!p.here(millis()) && !p.probePresent()); CHECK(!p.takeProbeFinished());
  CHECK(is(p.state(), "unknown"));
  CHECK(p.startProbe(42, true, 2)); reply(a); tick(100); CHECK(p.busy());
  reply(b); tick(100); CHECK(p.takeProbeFinished()); CHECK(p.here(millis()));
  CHECK(p.startProbe(43, false, 0)); p.setTarget(nullptr);
  CHECK(!p.here(millis()) && !p.probePresent() && !p.hasLastCheck());
  reply(b); tick(100); CHECK(!p.busy() && !p.takeProbeFinished());
  CHECK(is(p.state(), "not_configured") && !p.startProbe(44, false, 0));
  p.setTarget(a); CHECK(p.startProbe(44, false, 0)); p.setTarget(a);
  reply(a); tick(100); CHECK(!p.here(millis()) && !p.takeProbeFinished());
}
static void timeoutAndLate() {
  p.setTarget(a); CHECK(p.startProbe(51, false, 0)); tick(19999); CHECK(!p.takeProbeFinished());
  tick(1); CHECK(p.takeProbeFinished()); CHECK(!p.probePresent() && is(p.probeReason(), "timeout"));
  CHECK(p.probeElapsedMs() == 20000 && is(p.state(), "stalled"));
  tick(1000); CHECK(!p.takeProbeFinished()); CHECK(p.busy());
  CHECK(!p.startProbe(52, false, 0)); reply(b); tick(); CHECK(p.busy());
  reply(a); tick(); CHECK(!p.busy() && !p.here(millis()) && !p.takeProbeFinished());
  CHECK(p.startProbe(52, false, 0)); reply(a); tick(20000);
  CHECK(p.takeProbeFinished() && !p.probePresent() && is(p.probeReason(), "timeout"));
  CHECK(!p.busy());
  // Timer wrap and immediate start errors are fail-closed too.
  at(UINT32_MAX - 1000); CHECK(p.startProbe(53, false, 0)); reply(a); tick(1400);
  CHECK(p.takeProbeFinished() && p.probePresent() && p.probeElapsedMs() == 1400);
  NativeBt::state().probeResult = -1; CHECK(!p.startProbe(54, false, 0));
  CHECK(!p.busy() && !p.here(millis()) && is(p.state(), "radio_error"));
  CHECK(!p.takeProbeFinished()); NativeBt::state().probeResult = 0;
}
static void scanning() {
  CHECK(p.startScan(61, true, 109)); CHECK(p.busy() && !p.here(millis()));
  CHECK(!p.startProbe(62, true, 109));
  std::string name(47, 'x'); name += "\xe2\x82\xac"; // never split a 3-byte codepoint at byte 48
  for (int i = 0; i < 12; ++i) { found(i, -90 + i, i == 11 ? name.c_str() : "phone"); tick(); }
  found(11, -20, ""); tick(); found(11, -80, ""); tick();
  stop(); tick(); CHECK(p.takeScanFinished() && !p.takeScanFinished()); CHECK(!p.busy());
  CHECK(p.scanId() == 61 && p.scanFromBle() && p.scanSession() == 109);
  CHECK(p.scanCount() == 8 && p.scanTruncated() && is(p.scanState(), "complete"));
  CHECK(p.device(0).rssi == -20 && p.device(0).address[5] == 11 && strlen(p.device(0).name) == 47);
  for (unsigned i = 1; i < p.scanCount(); ++i) CHECK(p.device(i-1).rssi >= p.device(i).rssi);
  CHECK(p.device(99).address[0] == 0);
  CHECK(p.startScan(62, false, 0));
  // Bounded EIR parsing, including a malformed field that must not overread.
  uint8_t eir[] = {5, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, 'T', 'e', 's', 't', 0};
  esp_bt_gap_dev_prop_t prop = {ESP_BT_GAP_DEV_PROP_EIR, sizeof(eir), eir};
  esp_bt_gap_cb_param_t param = {}; param.disc_res.bda[0] = 0x40;
  param.disc_res.num_prop = 1; param.disc_res.prop = &prop;
  NativeBt::state().callback(ESP_BT_GAP_DISC_RES_EVT, &param); tick();
  CHECK(!strcmp(p.device(0).name, "Test"));
  eir[0] = 240; param.disc_res.bda[5] = 1;
  NativeBt::state().callback(ESP_BT_GAP_DISC_RES_EVT, &param); tick();
  CHECK(p.device(1).name[0] == 0); stop(); tick(); CHECK(p.takeScanFinished());
  CHECK(p.startScan(63, false, 0));
  // Flood bounded data queue; completion has its own queue and is never evicted.
  for (int i = 0; i < 100; ++i) found(i, -50, "");
  stop(); tick(); CHECK(p.takeScanFinished() && !p.busy() && p.scanTruncated());
  CHECK(p.startScan(64, false, 0)); tick(20000); CHECK(p.takeScanFinished());
  CHECK(p.busy() && is(p.scanState(), "failed") && is(p.scanError(), "timeout"));
  CHECK(NativeBt::state().cancels == 1); tick(1000); CHECK(!p.takeScanFinished());
  CHECK(!p.startScan(65, false, 0)); stop(); tick(); CHECK(!p.busy() && !p.takeScanFinished());
  NativeBt::state().scanResult = -1; CHECK(!p.startScan(66, false, 0));
  CHECK(!p.busy() && is(p.scanState(), "failed") && is(p.scanError(), "radio_error"));
  NativeBt::state().scanResult = 0;
}
static void utf8() {
  char out[49] = {};
  const char *text = "Phone \xf0\x9f\x92\xa7";
  CHECK(classicNameCopy(out, reinterpret_cast<const uint8_t *>(text), strlen(text)) == strlen(text));
  CHECK(!strcmp(out, text));
  for (const char *bad : {"\xc0\xaf", "\xed\xa0\x80", "\xe0\x80\x80", "\xf4\x90\x80\x80", "\xf0\x9f"})
    CHECK(classicNameCopy(out, reinterpret_cast<const uint8_t *>(bad), strlen(bad)) == 0);
}
static void persistence() {
  Preferences::reset(); Preferences legacy;
  legacy.putBytes("ble_mac", a, 6); legacy.putBytes("classic_mac", b, 6); legacy.putString("classic_name", "old alias");
  legacy.putBool("presence_enabled", false); legacy.putUShort("interval_min", 45);
  legacy.putLong("tare_offset", 100); legacy.putFloat("cal_factor", -2000);
  legacy.putFloat("baseline_weight", 700); legacy.putInt("last_day", 20261115);
  legacy.putFloat("daily_total", 120); legacy.putString("wifi_s0", "retained"); legacy.putString("wifi_p0", "password");
  BottleNvm n; CHECK(n.begin()); n.load(); CHECK(!n.data.hasClassicPhone && n.data.hasBleMac);
  CHECK(!n.stored("classic_mac") && !n.stored("classic_name"));
  const unsigned writes = Preferences::state().writes;
  std::string longName(47, 'x'); longName += "\xe2\x82\xac";
  CHECK(n.saveClassicPhone(b, longName.c_str())); CHECK(Preferences::state().writes == writes + 1);
  CHECK(Preferences::state().lastWrite == "classic_v1"); CHECK(n.stored("classic_mac") && n.stored("classic_name"));
  CHECK(n.data.hasClassicPhone && strlen(n.data.classicName) == 47);
  CHECK(n.data.hasBleMac && !memcmp(n.data.bleMac, a, 6));
  Preferences::state().fail = true; CHECK(!n.saveClassicPhone(a, "replacement")); CHECK(!n.clearClassicPhone());
  CHECK(!memcmp(n.data.classicMac, b, 6)); Preferences::state().fail = false;
  BottleNvm boot; boot.begin(); boot.load(); CHECK(boot.data.hasClassicPhone && !memcmp(boot.data.classicMac, b, 6));
  CHECK(boot.data.hasTare && boot.data.hasCal && boot.data.netCount == 1 && boot.data.intervalMin == 45);
  CHECK(boot.data.dailyTotalMl == 120 && boot.data.presenceEnabled);
  p.setTarget(boot.data.classicMac); CHECK(!p.here(millis()) && !p.hasLastCheck());
  CHECK(boot.saveClassicPhone(a, "replacement")); boot.load(); CHECK(!strcmp(boot.data.classicName, "replacement"));
  CHECK(!memcmp(boot.data.classicMac, a, 6));
  const unsigned clearWrites = Preferences::state().writes; CHECK(boot.clearClassicPhone());
  CHECK(Preferences::state().writes == clearWrites + 1); boot.load();
  CHECK(!boot.data.hasClassicPhone && !boot.stored("classic_mac") && !boot.stored("classic_name"));
  CHECK(boot.rawStored("classic_mac") && boot.data.hasBleMac && boot.data.netCount == 1);
  CHECK(boot.data.hasBaseline && boot.data.baselineWeight == 700 && boot.data.lastDay == 20261115);
  CHECK(boot.data.hasTare && boot.data.hasCal && boot.data.intervalMin == 45 && boot.data.dailyTotalMl == 120);
  CHECK(boot.data.classicMac[0] == 0 && boot.data.classicName[0] == 0);
  CHECK(boot.saveClassicPhone(a, "")); boot.load(); CHECK(boot.data.hasClassicPhone && boot.data.classicName[0] == 0);
  Preferences::state().keys["classic_v1"].bytes[15] ^= 1; boot.load();
  CHECK(!boot.data.hasClassicPhone && !boot.stored("classic_mac")); CHECK(boot.data.hasBleMac);
  const uint8_t zero[6] = {}, ones[6] = {255,255,255,255,255,255};
  CHECK(!boot.saveClassicPhone(nullptr, "x") && !boot.saveClassicPhone(a, nullptr));
  CHECK(!boot.saveClassicPhone(zero, "x") && !boot.saveClassicPhone(ones, "x"));
}
int main() {
  const std::pair<const char *, std::function<void()>> tests[] = {
    {"Classic begin fail-closed", initialization}, {"Classic async/matching/manual/expiry", asynchronousIdentityAndExpiry},
    {"Classic replacement/clear/same-target quarantine", replacementAndClearQuarantine},
    {"Classic timeout/late/wrong/wrap/radio failure", timeoutAndLate},
    {"Classic bounded inquiry/strongest/UTF8/timeout", scanning}, {"Classic UTF8 validation", utf8},
    {"Classic atomic CRC target/reboot/legacy/clear/failure", persistence}
  };
  for (const auto &test : tests) {
    try { test.second(); std::cout << "PASS " << test.first << '\n'; }
    catch (const std::exception &e) { std::cerr << "FAIL " << test.first << ": " << e.what() << '\n'; return 1; }
  }
  CHECK(NativeArduino::delays() == 0);
  std::cout << "7 Classic tests passed\n";
}
