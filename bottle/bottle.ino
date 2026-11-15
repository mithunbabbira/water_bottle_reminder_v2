// Standalone ESP32-WROOM hydration reminder. Protocol: docs/PROTOCOL.md.
// Hardware access, flash writes and commands are owned solely by loop().
#include <ArduinoJson.h>
#include "JsonValidation.h"
#include "BootWifiTime.h"
#include "BottleBle.h"
#include "Hydration.h"
#include "PhonePresence.h"
#include "WifiDiscovery.h"

BottleNvm nvm;
Scale scale;
Outputs outputs;
Hydration hydration;
ClockService clockSvc;
BootWifiTime wifiTime;
WifiDiscovery discovery;
BottleBle ble;
PhonePresence presence;
bool ready = false, bleReady = false;
uint32_t lastStatusMs = 0;
String serialLine;
bool serialDiscard = false;
int maintenanceId = 0;
bool maintenanceTare = false;
uint32_t maintenanceSession = 0;
bool maintenanceBle = false;
bool replyBle = false;

void sendJson(const JsonDocument &doc, bool toBle = true) {
  String line;
  serializeJson(doc, line);
  Serial.println(line);
  if (toBle && bleReady && !ble.send(line)) Serial.println("{\"type\":\"error\",\"error\":\"BLE output queue full\"}");
}
void ack(int id, bool ok, const char *error = "") {
  JsonDocument d;
  d["type"] = "ack"; d["id"] = id; d["ok"] = ok;
  if (!ok) d["error"] = error;
  sendJson(d,replyBle);
}
void settingsJson(JsonObject s) {
  const auto &v = nvm.data;
  s["start_min"] = v.startMin; s["end_min"] = v.endMin;
  s["interval_min"] = v.intervalMin; s["beep_type"] = v.beepType; s["dnd"] = v.dnd;
  s["tz_min"] = v.tzMin;
  s["color"] = (uint32_t(v.colorR) << 16) | (uint32_t(v.colorG) << 8) | v.colorB;
  s["reminder_color"] = v.reminderColor; s["missing_color"] = v.missingColor;
  s["missing_sec"] = v.missingSec; s["beep_delay_sec"] = v.beepDelaySec;
  s["drink_grams"] = v.drinkGrams; s["settle_ms"] = v.settleMs;
  s["presence_enabled"] = v.presenceEnabled;
}
String phoneMac() {
  if (!nvm.data.hasBleMac) return "";
  char mac[18];
  snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x", nvm.data.bleMac[0], nvm.data.bleMac[1],
           nvm.data.bleMac[2], nvm.data.bleMac[3], nvm.data.bleMac[4], nvm.data.bleMac[5]);
  return String(mac);
}
void status() {
  JsonDocument d;
  d["type"] = "status"; d["protocol"] = 2; d["firmware"] = "2.0.0"; d["ready"] = ready;
  d["clock_valid"] = clockSvc.valid(); d["time_source"] = wifiTime.source();
  d["epoch"] = (int64_t)time(nullptr); d["tz_min"] = nvm.data.tzMin;
  d["active"] = ClockService::inActiveWindow(nvm.data, clockSvc.clock());
  int32_t raw; float grams;
  const bool haveRaw = scale.latestRaw(&raw, 1000);
  if (haveRaw) d["raw"] = raw; else d["raw"] = nullptr;
  const bool haveGrams = scale.latestGrams(&grams, SAMPLE_MAX_AGE_MS);
  if (haveGrams) d["weight_g"] = grams; else d["weight_g"] = nullptr;
  d["scale_state"] = scale.busy() ? "measuring" : !haveRaw ? "no_data" : scale.fault() ? "fault" :
                     !nvm.data.hasTare ? "need_tare" : !nvm.data.hasCal ? "need_cal" : "ready";
  d["state"] = scale.busy() ? "settling" : hydration.stateName();
  if (hydration.hasBaseline()) d["baseline_g"] = hydration.baseline(); else d["baseline_g"] = nullptr;
  d["today_ml"] = hydration.dailyMl(); d["last_drink_ml"] = hydration.lastDrinkMl();
  d["next_check_sec"] = hydration.nextCheckSeconds(millis());
  d["phone_connected"] = bleReady && ble.connected(); d["phone_present"] = presence.here(millis());
  if (nvm.data.hasBleMac) d["phone_mac"] = phoneMac(); else d["phone_mac"] = nullptr;
  d["wifi_scan_state"] = discovery.state();
  d["wifi_state"] = wifiTime.state(); d["wifi_error"] = wifiTime.error(); d["wifi_ssid"] = wifiTime.ssid();
  settingsJson(d["settings"].to<JsonObject>());
  auto networks = d["networks"].to<JsonArray>();
  for (uint8_t i = 0; i < nvm.data.netCount; ++i) {
    auto net = networks.add<JsonObject>(); net["ssid"] = nvm.data.nets[i].ssid;
    net["secured"] = nvm.data.nets[i].pass[0] != 0;
  }
  d["storage_fault"] = hydration.storageFault(); d["storage_pending"] = hydration.persistencePending();
  d["uptime_sec"] = millis() / 1000; d["free_heap"] = ESP.getFreeHeap();
  sendJson(d); lastStatusMs = millis();
}
void event(const char *name, float ml = 0) {
  JsonDocument d; d["type"] = "event"; d["name"] = name; d["ml"] = ml; sendJson(d);
}

// Dump incrementally: no delay(), no lost scale samples, no passwords.
int dumpId = 0, dumpIndex = -1;
bool dumpBle = false;
JsonDocument dump;
void beginDump(int id) {
  dump.clear();
  auto &v = nvm.data;
  auto put = [&](const char *key, JsonVariantConst value, bool stored) {
    auto e = dump[key].to<JsonObject>(); e["value"] = value; e["stored"] = stored;
  };
  auto scalar = [&](const char *key, auto value, bool stored) {
    JsonDocument temp; temp.set(value); put(key, temp.as<JsonVariantConst>(), stored);
  };
  scalar("schema", BOTTLE_SCHEMA_VER, nvm.stored("schema"));
  scalar("tare_offset", v.tareOffset, v.hasTare); scalar("cal_factor", v.calFactor, v.hasCal);
  if (v.hasBaseline) scalar("baseline_weight", v.baselineWeight, true);
  else { JsonDocument nullDoc; put("baseline_weight", nullDoc.as<JsonVariantConst>(), false); }
  scalar("daily_total", v.dailyTotalMl, nvm.stored("daily_total"));
  scalar("last_day", v.lastDay, nvm.stored("last_day"));
  scalar("ble_mac", phoneMac(), v.hasBleMac); scalar("ble_irk", v.hasIrk ? "set" : "empty", v.hasIrk);
  JsonDocument s; settingsJson(s.to<JsonObject>());
  for (JsonPair p : s.as<JsonObject>()) put(p.key().c_str(), p.value(), nvm.settingStored(p.key().c_str()));
  scalar("net_count", v.netCount, nvm.stored("net_count"));
  scalar("sta_ssid", v.staSsid, nvm.stored("sta_ssid"));
  scalar("sta_pass", v.staPass[0] ? "set" : "empty", nvm.stored("sta_pass"));
  for (uint8_t i = 0; i < BOTTLE_MAX_NETWORKS; ++i) {
    char sk[12], pk[12]; snprintf(sk, sizeof(sk), "wifi_s%u", i); snprintf(pk, sizeof(pk), "wifi_p%u", i);
    scalar(sk, i < v.netCount ? v.nets[i].ssid : "", i < v.netCount);
    scalar(pk, i < v.netCount && v.nets[i].pass[0] ? "set" : "empty", i < v.netCount);
  }
  scalar("ap_ssid", v.apSsid, nvm.stored("ap_ssid"));
  scalar("ap_pass", v.apPass[0] ? "set" : "empty", nvm.stored("ap_pass"));
  scalar("ap_behavior", v.apBehavior, nvm.stored("ap_behavior"));
  dumpId = id; dumpIndex = 0; dumpBle = replyBle;
  JsonDocument start; start["type"] = "nvs.begin"; start["id"] = id; sendJson(start,dumpBle);
}
void tickDump() {
  if (dumpIndex < 0 || !ble.txIdle()) return;
  int i = 0;
  for (JsonPair p : dump.as<JsonObject>()) {
    if (i++ != dumpIndex) continue;
    JsonDocument d; d["type"] = "nvs.entry"; d["id"] = dumpId; d["key"] = p.key();
    d["value"] = p.value()["value"]; d["stored"] = p.value()["stored"];
    sendJson(d,dumpBle); ++dumpIndex; return;
  }
  JsonDocument end; end["type"] = "nvs.end"; end["id"] = dumpId; sendJson(end,dumpBle);
  dumpIndex = -1; dump.clear();
}

bool settingsCommand(JsonObjectConst args) {
  BottleNvmData v = nvm.data;
  static const char *allowed[] = {"id","cmd","start_min","end_min","interval_min","beep_type","dnd",
    "tz_min","color","reminder_color","missing_color","missing_sec","beep_delay_sec","drink_grams",
    "settle_ms","presence_enabled"};
  for (JsonPairConst p : args) {
    bool known = false;
    for (const char *key : allowed) if (!strcmp(p.key().c_str(),key)) known = true;
    if (!known || p.value().isNull()) return false;
  }
  // Validate before narrowing integers (and reject wrong JSON types).
  auto number = [&](const char *key, double lo, double hi, bool integral = true) {
    if (args[key].isNull()) return true;
    if (!args[key].is<double>() || args[key].is<bool>()) return false;
    const double n = args[key].as<double>();
    return isfinite(n) && n >= lo && n <= hi && (!integral || floor(n) == n);
  };
  if (!number("start_min",0,1439) || !number("end_min",0,1439) || !number("interval_min",1,240) ||
      !number("beep_type",0,3) || !number("tz_min",-720,840) || !number("color",0,16777215) ||
      !number("reminder_color",0,16777215) || !number("missing_color",0,16777215) ||
      !number("missing_sec",10,3600) || !number("beep_delay_sec",0,30) ||
      !number("drink_grams",5,500) || !number("settle_ms",500,5000)) return false;
  if ((!args["dnd"].isNull() && !args["dnd"].is<bool>()) ||
      (!args["presence_enabled"].isNull() && !args["presence_enabled"].is<bool>())) return false;
#define APPLY(key,field) if (!args[key].isNull()) v.field = args[key].as<decltype(v.field)>();
  APPLY("start_min",startMin) APPLY("end_min",endMin) APPLY("interval_min",intervalMin)
  APPLY("beep_type",beepType) APPLY("dnd",dnd) APPLY("tz_min",tzMin)
  APPLY("reminder_color",reminderColor) APPLY("missing_color",missingColor)
  APPLY("missing_sec",missingSec) APPLY("beep_delay_sec",beepDelaySec)
  APPLY("drink_grams",drinkGrams) APPLY("settle_ms",settleMs) APPLY("presence_enabled",presenceEnabled)
#undef APPLY
  if (v.tzMin % 15) return false;
  if (!args["color"].isNull()) {
    const uint32_t c = args["color"].as<uint32_t>(); v.colorR = c >> 16; v.colorG = c >> 8; v.colorB = c;
  }
  const bool resetInterval = v.intervalMin != nvm.data.intervalMin;
  if (!nvm.saveSettings(v)) return false;
  clockSvc.setTz(v.tzMin);
  if (resetInterval) hydration.useSavedInterval(nvm);
  return true;
}
void processCommand(const char *text, bool fromBle = false) {
  replyBle = fromBle;
  JsonDocument d;
  auto err = deserializeJson(d, text);
  if (err || !singleJsonObject(text) || !validJsonStrings(d.as<JsonVariantConst>()) ||
      !d.is<JsonObject>() || !d["id"].is<int>() || !d["cmd"].is<const char *>()) {
    ack(0,false,"Expected newline-delimited JSON with integer id and cmd."); return;
  }
  const int id = d["id"]; const char *cmd = d["cmd"];
  if (!strcmp(cmd,"status")) { ack(id,true); status(); return; }
  if (!ready) { ack(id,false,"Storage unavailable. Restart the bottle."); return; }
  if (scale.busy()) { ack(id,false,"Scale measurement in progress. Please wait."); return; }
  if (!strcmp(cmd,"settings")) {
    if (!settingsCommand(d.as<JsonObjectConst>())) { ack(id,false,"Invalid settings or flash save failed."); return; }
  } else if (!strcmp(cmd,"nvs")) {
    if (dumpIndex >= 0) { ack(id,false,"Diagnostics already in progress."); return; }
    ack(id,true); beginDump(id); return;
  } else if (!strcmp(cmd,"wifi.scan")) {
    if (wifiTime.busy() || discovery.busy()) { ack(id,false,"Wi-Fi is busy. Wait for the current check to finish."); return; }
    discovery.start(id,fromBle,ble.session());
  } else if (!strcmp(cmd,"wifi.add") || !strcmp(cmd,"wifi.remove")) {
    if (wifiTime.busy() || discovery.busy()) { ack(id,false,"Wi-Fi check in progress. Please wait."); return; }
    if (!d["ssid"].is<const char *>() || !strlen(d["ssid"])) { ack(id,false,"Enter a Wi-Fi network name."); return; }
    const char *ssid = d["ssid"];
    if (!strcmp(cmd,"wifi.add")) {
      if (!d["password"].is<const char *>() || !nvm.saveNet(ssid,d["password"])) {
        ack(id,false,"Could not save. Use a 1–32 byte name, empty or 8–63 byte password, max four networks."); return;
      }
      wifiTime.start(nvm);
    } else if (!nvm.removeNet(ssid)) { ack(id,false,"Network not found or flash save failed."); return; }
  } else if (!strcmp(cmd,"wifi.sync")) {
    if (discovery.busy() || !wifiTime.start(nvm)) { ack(id,false,"Already checking Wi-Fi, or no saved networks."); return; }
  } else if (!strcmp(cmd,"time.set")) {
    if (!d["epoch"].is<int64_t>() || d["epoch"].as<int64_t>() < 1700000000LL || d["epoch"].as<int64_t>() > 4102444800LL) {
      ack(id,false,"Invalid UTC time."); return;
    }
    if (!d["tz_min"].isNull()) {
      JsonDocument s; s["tz_min"] = d["tz_min"];
      if (!settingsCommand(s.as<JsonObjectConst>())) { ack(id,false,"Invalid timezone."); return; }
    }
    if (!clockSvc.setEpoch((time_t)d["epoch"].as<int64_t>())) { ack(id,false,"Could not set system time."); return; }
    wifiTime.appTimeSet();
  } else if (!strcmp(cmd,"tare") || !strcmp(cmd,"calibrate")) {
    const bool tare = !strcmp(cmd,"tare");
    const float grams = d["grams"] | 0.0f;
    if (!tare && (!nvm.data.hasTare || !d["grams"].is<double>() || !isfinite(grams) || grams <= 0 || grams > 10000)) {
      ack(id,false,"Zero the empty base first, then use a known 1–10000 g weight."); return;
    }
    if (!scale.startOperation(tare ? 0 : grams)) { ack(id,false,"Scale busy."); return; }
    maintenanceId = id; maintenanceTare = tare; maintenanceBle = fromBle; maintenanceSession = ble.session();
    return; // ACK only after sampling + flash save succeed.
  } else if (!strcmp(cmd,"phone.clear")) {
    if (!nvm.clearBleMac()) { ack(id,false,"Could not clear saved peer. Flash save failed."); return; }
  } else { ack(id,false,"Unknown command. Update the app/firmware together."); return; }
  ack(id,true); status();
}
void pollSerial() {
  // Bound input work each loop so even a noisy host can't starve the scale.
  for (int i = 0; i < 128 && Serial.available(); ++i) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      if (serialDiscard) ack(0,false,"Command too long.");
      else if (serialLine.length()) processCommand(serialLine.c_str());
      serialLine = ""; serialDiscard = false;
    } else if (serialLine.length() >= BottleBle::kMaxLine || c == 0) serialDiscard = true;
    else if (!serialDiscard) serialLine += c;
  }
}
void setup() {
  Serial.begin(115200);
  outputs.begin(); scale.begin();
  ready = nvm.begin();
  bleReady = ble.begin();
  if (!ready) { status(); return; } // Remain reachable to report storage failure.
  nvm.load(); clockSvc.begin(nvm.data.tzMin); hydration.begin(nvm.data);
  wifiTime.begin(nvm,clockSvc);
  status();
}
void loop() {
  if (!ready) {
    outputs.show(0,0,0,false,false,BEEP_OFF);
    if (bleReady) {
      ble.tick();
      static BottleBle::Command command;
      if (ble.txIdle() && ble.popCommand(command)) {
        processCommand(command.text,true); memset(command.text,0,sizeof(command.text));
      }
    }
    pollSerial(); delay(1); return;
  }
  scale.poll(nvm.data); clockSvc.tick(); wifiTime.tick(clockSvc);
  if (bleReady) {
    ble.tick();
    static bool lastLink = false;
    BottleBle::Link link;
    while (ble.popLink(link)) {
      lastLink = link.connected;
      if (link.connected) {
        presence.connected(millis());
        if (!nvm.data.hasBleMac || memcmp(nvm.data.bleMac,link.mac,6)) {
          if (!nvm.saveBleMac(link.mac)) event("storage_error");
        }
      } else {
        presence.disconnected(millis());
        if (dumpBle) { dumpIndex = -1; dump.clear(); }
      }
    }
    // Reconcile BOTH directions even if a disconnect event was dropped.
    const bool linked = ble.connected();
    if (linked) presence.connected(millis());
    else if (lastLink) presence.disconnected(ble.disconnectedAt());
    lastLink = linked;
    if (ble.overflowed()) { replyBle = true; ack(0,false,"Command queue full or frame too long. Retry."); }
    static BottleBle::Command command;
    if (ble.txIdle() && ble.popCommand(command)) {
      processCommand(command.text,true); memset(command.text,0,sizeof(command.text));
    }
  }
  pollSerial();
  if (scale.busy()) {
    outputs.show(0,0,0,false,false,BEEP_OFF);
    int result = scale.finishOperation(nvm);
    if (result) {
      if (result > 0 && !hydration.forgetBaseline(nvm)) result = -1;
      replyBle = maintenanceBle;
      if (!maintenanceBle || maintenanceSession == ble.session()) ack(maintenanceId,result > 0,result < 0 ? "Scale unstable or unavailable. Keep it still, check wiring and retry." : "");
      status();
    }
  } else {
    hydration.tick(millis(),scale,nvm,outputs,clockSvc.clock(),presence.here(millis()));
    const uint16_t events = hydration.takeEvents();
    if (events & HydrationEngine::Drink) event("drink",hydration.lastDrinkMl());
    if (events & HydrationEngine::Refill) event("refill");
    if (events & HydrationEngine::Lifted) event("lifted");
    if (events & HydrationEngine::Returned) event("returned");
    if (events & HydrationEngine::Reminder) event("reminder");
    if (events & HydrationEngine::MissingAlert) event("missing_alert");
    if (events & HydrationEngine::Paused) event("paused");
    if (events & HydrationEngine::ScaleError) event("scale_error");
    static bool storageFault = false;
    if (hydration.storageFault() && !storageFault) event("storage_error");
    storageFault = hydration.storageFault();
  }
  discovery.tick();
  if (discovery.takeFinished()) {
    JsonDocument result; result["type"] = "wifi.scan"; result["id"] = discovery.id();
    result["ok"] = !discovery.error()[0];
    if (discovery.error()[0]) result["error"] = discovery.error();
    result["truncated"] = discovery.truncated(); result["hidden_count"] = discovery.hiddenCount();
    auto nets = result["networks"].to<JsonArray>();
    for (int i = 0; i < discovery.count(); ++i) {
      const auto &n = discovery.network(i); auto item = nets.add<JsonObject>();
      item["ssid"] = n.ssid; item["rssi"] = n.rssi; item["secured"] = n.secured;
    }
    sendJson(result,discovery.fromBle() && ble.session() == discovery.session());
  }
  tickDump();
  if (bleReady && ble.connected() && dumpIndex < 0 && ble.txIdle() && millis()-lastStatusMs >= 2000) status();
  delay(1); // yield to radio/RTOS without blocking sensor state transitions
}
