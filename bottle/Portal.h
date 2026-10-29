#pragma once

#include "Hydration.h"
#include "Scale.h"
#include "WebPageGz.h"

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// Serves the setup page and live hydration status.
// The phone list and presence come from the sketch so serial and the page share BLE state.

class Portal {
 public:
  using PhoneList = String (*)();
  using HomeCheck = AtHome (*)();

  void begin(BottleNvm *nvm, Scale *scale, Hydration *hydration, Clock *clock, PhoneList phones, HomeCheck homeCheck) {
    nvm_ = nvm;
    scale_ = scale;
    hydration_ = hydration;
    clock_ = clock;
    phones_ = phones;
    homeCheck_ = homeCheck;
    WiFi.persistent(false);
    WiFi.setHostname("bottle");
    startRadio();
    server_.on("/", [this]() { sendPage(); });
    server_.on("/generate_204", [this]() { sendHint(); });
    server_.on("/gen_204", [this]() { sendHint(); });
    server_.on("/hotspot-detect.html", [this]() { sendHint(); });
    server_.on("/canonical.html", [this]() { sendHint(); });
    server_.on("/connecttest.txt", [this]() { sendHint(); });
    server_.on("/ncsi.txt", [this]() { sendHint(); });
    server_.on("/success.txt", [this]() { sendHint(); });
    server_.on("/api", [this]() { sendStatus(); });
    server_.on("/phones", [this]() { sendPhones(); });
    server_.on("/wifis", [this]() { sendWifis(); });
    server_.on("/wifi/remove", HTTP_POST, [this]() { handleRemoveWifi(); });
    server_.on("/phone", HTTP_POST, [this]() { handlePhone(); });
    server_.on("/tare", HTTP_POST, [this]() { handleTare(); });
    server_.on("/settings", HTTP_POST, [this]() { handleSettings(); });
    server_.onNotFound([this]() {
      if (server_.uri() == "/favicon.ico") {
        armReply();
        server_.send(204, "text/plain", "");
        return;
      }
      server_.sendHeader("Connection", "close");
      server_.sendHeader("Location", "/", true);
      server_.send(302, "text/plain", "");
    });
    server_.begin();
    Serial.println("page ready");
    if (nvm_->data.staSsid[0] == '\0') {
      return;
    }
    const uint32_t wifiStart = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 25000) {
      server_.handleClient();
      delay(20);
    }
    if (WiFi.status() != WL_CONNECTED) {
      return;
    }
    announceLan();
    const uint32_t timeStart = millis();
    while (millis() - timeStart < 10000) {
      server_.handleClient();
      readClock();
      if (clock_->valid) {
        break;
      }
      delay(20);
    }
  }

  void tick() {
    server_.handleClient();
    if (apOn_) {
      dns_.processNextRequest();
    }
    if (restartAtMs_ != 0 && (int32_t)(millis() - restartAtMs_) >= 0) {
      restartAtMs_ = 0;
      restartRadio();
    }
    const bool connected = WiFi.status() == WL_CONNECTED;
    if (connected && !clockStarted_) {
      announceLan();
    }
    readClock();
    const uint32_t now = millis();
    const uint8_t behavior = nvm_->data.apBehavior;
    if (!connected && nvm_->data.staSsid[0] && !apOn_ && now - staStartedMs_ > 15000 &&
        (behavior == BOTTLE_AP_BOOT_NO_CONN || behavior == BOTTLE_AP_NO_CONN || behavior == BOTTLE_AP_TEMPORARY)) {
      openAp();
      if (behavior == BOTTLE_AP_TEMPORARY) {
        apCloseMs_ = now + 300000;
      }
    }
    if (wasConnected_ && !connected && behavior == BOTTLE_AP_NO_CONN) {
      openAp();
    }
    if (apOn_ && behavior == BOTTLE_AP_TEMPORARY && apCloseMs_ != 0 && (int32_t)(now - apCloseMs_) >= 0 && !connected) {
      closeAp();
    }
    wasConnected_ = connected;
    if (apOn_ && now - lastApLogMs_ > 5000) {
      lastApLogMs_ = now;
      Serial.print("ap up, clients ");
      Serial.println(WiFi.softAPgetStationNum());
    }
  }

  void restartRadio() {
    closeAp();
    WiFi.disconnect(true);
    clockStarted_ = false;
    clock_->valid = false;
    startRadio();
  }

  void printStatus() {
    char body[896];
    writeStatus(body, sizeof(body));
    Serial.println(body);
  }

  void printWifis() { Serial.println(scanWifisJson()); }

 private:
  BottleNvm *nvm_ = nullptr;
  Scale *scale_ = nullptr;
  Hydration *hydration_ = nullptr;
  Clock *clock_ = nullptr;
  PhoneList phones_ = nullptr;
  HomeCheck homeCheck_ = nullptr;
  WebServer server_{80};
  DNSServer dns_;
  bool apOn_ = false;
  bool wasConnected_ = false;
  bool clockStarted_ = false;
  uint32_t staStartedMs_ = 0;
  uint32_t apCloseMs_ = 0;
  uint32_t lastApLogMs_ = 0;
  uint32_t restartAtMs_ = 0;

  int pickSavedNet() {
    if (nvm_->data.netCount == 0) {
      return -1;
    }
    if (nvm_->data.netCount == 1) {
      return 0;
    }
    const int n = WiFi.scanNetworks(false, false);
    int bestIdx = 0;
    int32_t bestRssi = -1000;
    for (int i = 0; i < n; ++i) {
      const String found = WiFi.SSID(i);
      const int32_t rssi = WiFi.RSSI(i);
      for (uint8_t k = 0; k < nvm_->data.netCount; ++k) {
        if (found == nvm_->data.nets[k].ssid && rssi > bestRssi) {
          bestRssi = rssi;
          bestIdx = (int)k;
        }
      }
    }
    WiFi.scanDelete();
    return bestIdx;
  }

  void armReply() {
    server_.sendHeader("Connection", "close");
    server_.sendHeader("Cache-Control", "no-store");
  }

  void announceLan() {
    WiFi.setSleep(WIFI_PS_NONE);
    WiFi.setTxPower(apOn_ ? WIFI_POWER_8_5dBm : WIFI_POWER_15dBm);
    server_.begin();
    MDNS.end();
    if (MDNS.begin("bottle")) {
      MDNS.addService("http", "tcp", 80);
    }
    clockStarted_ = true;
    applyClock();
    Serial.print("wifi ");
    Serial.print(WiFi.SSID());
    Serial.print(" ");
    Serial.print(WiFi.localIP());
    Serial.print(" rssi ");
    Serial.print(WiFi.RSSI());
    Serial.println(" (http://bottle.local/)");
  }

  void startRadio() {
    staStartedMs_ = millis();
    WiFi.setSleep(WIFI_PS_NONE);
    const bool hasSta = nvm_->data.netCount > 0;
    const bool alwaysAp = nvm_->data.apBehavior == BOTTLE_AP_ALWAYS;
    if (!hasSta || alwaysAp) {
      WiFi.mode(hasSta ? WIFI_AP_STA : WIFI_AP);
      openAp();
      if (hasSta) {
        const int idx = pickSavedNet();
        WiFi.begin(nvm_->data.nets[idx].ssid, nvm_->data.nets[idx].pass);
        Serial.print("joining ");
        Serial.println(nvm_->data.nets[idx].ssid);
      }
      return;
    }
    WiFi.mode(WIFI_STA);
    WiFi.setTxPower(WIFI_POWER_15dBm);
    const int idx = pickSavedNet();
    WiFi.begin(nvm_->data.nets[idx].ssid, nvm_->data.nets[idx].pass);
    Serial.print("joining ");
    Serial.println(nvm_->data.nets[idx].ssid);
  }

  void openAp() {
    if (apOn_) {
      return;
    }
    if (WiFi.getMode() == WIFI_STA) {
      WiFi.mode(WIFI_AP_STA);
    }
    nvm_->data.apPass[0] = '\0';
    delay(200);
    // Fixed name so a phone does not keep the old locked network.
    const bool opened = WiFi.softAP("bottle", nullptr, 1, 0, 4, false, WIFI_AUTH_OPEN);
    if (!opened) {
      Serial.println("setup network failed");
      return;
    }
    WiFi.setSleep(WIFI_PS_NONE);
    WiFi.softAPbandwidth(WIFI_BW_HT20);
    const bool txOk = WiFi.setTxPower(WIFI_POWER_8_5dBm);
    apOn_ = true;
    dns_.start(53, "*", WiFi.softAPIP());
    Serial.print("setup bottle open, channel 1, tx ");
    Serial.print(txOk ? "ok" : "not set");
    Serial.print(", at ");
    Serial.println(WiFi.softAPIP());
  }

  void closeAp() {
    if (!apOn_) {
      return;
    }
    dns_.stop();
    WiFi.softAPdisconnect(true);
    apOn_ = false;
  }

  void applyClock() {
    configTime((long)nvm_->data.tzMin * 60L, 0, "pool.ntp.org", "time.google.com");
    readClock();
  }

  void readClock() {
    struct tm local;
    if (!getLocalTime(&local, 0)) {
      return;
    }
    clock_->valid = true;
    clock_->hour = local.tm_hour;
    clock_->minute = local.tm_min;
    clock_->day = (local.tm_year + 1900) * 1000 + local.tm_yday;
  }

  static void escapeJson(char *dest, size_t destLen, const char *src) {
    size_t used = 0;
    for (const char *p = src; p && *p && used + 2 < destLen; ++p) {
      if (*p == '"' || *p == '\\') {
        dest[used++] = '\\';
      }
      dest[used++] = *p;
    }
    dest[used] = '\0';
  }

  void writeStatus(char *out, size_t outLen) {
    float grams = 0;
    const bool hasGrams = scale_->latestGrams(&grams, 2000);
    char gramText[16];
    if (hasGrams) {
      snprintf(gramText, sizeof(gramText), "%.1f", grams);
    } else {
      strncpy(gramText, "null", sizeof(gramText));
      gramText[sizeof(gramText) - 1] = '\0';
    }
    const String liveSsid = WiFi.status() == WL_CONNECTED ? WiFi.SSID() : String(nvm_->data.staSsid);
    char ssid[BOTTLE_SSID_LEN * 2];
    escapeJson(ssid, sizeof(ssid), liveSsid.c_str());

    char netsJson[320];
    size_t pos = 0;
    netsJson[pos++] = '[';
    for (uint8_t i = 0; i < nvm_->data.netCount; ++i) {
      char item[BOTTLE_SSID_LEN * 2];
      escapeJson(item, sizeof(item), nvm_->data.nets[i].ssid);
      const int wrote = snprintf(netsJson + pos, sizeof(netsJson) - pos, "%s\"%s\"", i == 0 ? "" : ",", item);
      if (wrote <= 0 || (size_t)wrote >= sizeof(netsJson) - pos - 2) {
        break;
      }
      pos += (size_t)wrote;
    }
    netsJson[pos++] = ']';
    netsJson[pos] = '\0';

    char lastText[16];
    if (hydration_ != nullptr && hydration_->hasLastDrink()) {
      snprintf(lastText, sizeof(lastText), "%.1f", hydration_->lastDrinkMl());
    } else {
      strncpy(lastText, "null", sizeof(lastText));
      lastText[sizeof(lastText) - 1] = '\0';
    }
    const float todayMl = hydration_ != nullptr ? hydration_->dailyMl() : nvm_->data.dailyTotalMl;
    const bool isMissing = hydration_ != nullptr ? hydration_->missing() : (hasGrams && grams < MISSING_GRAMS);
    const AtHome home = homeCheck_ != nullptr ? homeCheck_() : AtHome{};

    const IPAddress ip = WiFi.status() == WL_CONNECTED ? WiFi.localIP() : WiFi.softAPIP();
    snprintf(out, outLen,
             "{\"grams\":%s,\"fault\":%s,\"missing\":%s,\"today\":%.1f,\"last\":%s,\"watching\":%s,\"home\":%s,"
             "\"dnd\":%s,\"r\":%u,\"g\":%u,\"b\":%u,\"start\":%u,\"end\":%u,\"interval\":%u,\"beep\":%u,\"ap\":%u,\"tz\":%d,"
             "\"mac\":\"%s\",\"ssid\":\"%s\",\"nets\":%s,\"wifi\":%s,\"ip\":\"%u.%u.%u.%u\"}",
             gramText, scale_->fault() ? "true" : "false", isMissing ? "true" : "false", todayMl, lastText,
             home.watching ? "true" : "false", home.home ? "true" : "false",
             nvm_->data.dnd ? "true" : "false", nvm_->data.colorR, nvm_->data.colorG, nvm_->data.colorB,
             nvm_->data.startMin, nvm_->data.endMin, nvm_->data.intervalMin, nvm_->data.beepType, nvm_->data.apBehavior,
             nvm_->data.tzMin,
             macText(), ssid, netsJson, WiFi.status() == WL_CONNECTED ? "true" : "false", ip[0], ip[1], ip[2], ip[3]);
  }

  void sendHint() {
    server_.sendHeader("Connection", "close");
    server_.sendHeader("Location", "http://192.168.4.1/", true);
    server_.send(302, "text/plain", "");
  }

  void sendPage() {
    armReply();
    server_.sendHeader("Content-Encoding", "gzip");
    server_.send_P(200, "text/html", (PGM_P)BOTTLE_PAGE_GZ, BOTTLE_PAGE_GZ_LEN);
  }

  void sendPhones() {
    armReply();
    server_.send(200, "application/json", phones_ != nullptr ? phones_() : "[]");
  }

  String scanWifisJson() {
    const int n = WiFi.scanNetworks(false, false);
    String json = "[";
    bool first = true;
    for (int i = 0; i < n; ++i) {
      const String ssid = WiFi.SSID(i);
      if (ssid.length() == 0 || ssid == "bottle") {
        continue;
      }
      bool seen = false;
      for (int j = 0; j < i; ++j) {
        if (WiFi.SSID(j) == ssid) {
          seen = true;
          break;
        }
      }
      if (seen) {
        continue;
      }
      char escaped[BOTTLE_SSID_LEN * 2];
      escapeJson(escaped, sizeof(escaped), ssid.c_str());
      if (!first) {
        json += ",";
      }
      first = false;
      json += "{\"ssid\":\"";
      json += escaped;
      json += "\",\"rssi\":";
      json += String(WiFi.RSSI(i));
      json += ",\"open\":";
      json += (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "true" : "false";
      json += "}";
    }
    json += "]";
    WiFi.scanDelete();
    return json;
  }

  void sendWifis() {
    armReply();
    server_.send(200, "application/json", scanWifisJson());
  }

  void handleRemoveWifi() {
    const String ssid = server_.arg("ssid");
    if (!nvm_->removeNet(ssid.c_str())) {
      armReply();
      server_.send(400, "text/plain", "not found");
      return;
    }
    armReply();
    server_.send(200, "text/plain", "ok");
    if (WiFi.status() == WL_CONNECTED && WiFi.SSID() == ssid) {
      restartAtMs_ = millis() + 500;
    }
  }

  void handlePhone() {
    uint8_t bytes[6];
    if (!parseMac(server_.arg("mac").c_str(), bytes)) {
      armReply();
      server_.send(400, "text/plain", "bad address");
      return;
    }
    if (!nvm_->saveBleMac(bytes)) {
      armReply();
      server_.send(500, "text/plain", "save failed");
      return;
    }
    armReply();
    server_.send(200, "text/plain", "ok");
  }

  void handleTare() {
    if (scale_->tare(*nvm_)) {
      if (hydration_ != nullptr) {
        hydration_->forgetBaseline(*nvm_);
      } else {
        nvm_->clearBaseline();
      }
      armReply();
      server_.send(200, "text/plain", "ok");
      return;
    }
    armReply();
    server_.send(500, "text/plain", "tare failed");
  }

  void handleSettings() {
    BottleNvmData &data = nvm_->data;
    nvm_->saveDnd(server_.hasArg("dnd"));

    unsigned int r = data.colorR, g = data.colorG, b = data.colorB;
    const String color = server_.arg("color");
    if (sscanf(color.c_str(), "#%02x%02x%02x", &r, &g, &b) == 3) {
      nvm_->saveColor((uint8_t)r, (uint8_t)g, (uint8_t)b);
    }

    int sh = 0, sm = 0, eh = 0, em = 0;
    if (sscanf(server_.arg("start").c_str(), "%d:%d", &sh, &sm) == 2 &&
        sscanf(server_.arg("end").c_str(), "%d:%d", &eh, &em) == 2 &&
        sh >= 0 && sh <= 23 && sm >= 0 && sm <= 59 && eh >= 0 && eh <= 23 && em >= 0 && em <= 59) {
      nvm_->saveWindow((uint16_t)(sh * 60 + sm), (uint16_t)(eh * 60 + em));
    }

    if (server_.hasArg("tz")) {
      const int tz = server_.arg("tz").toInt();
      if (tz != nvm_->data.tzMin && nvm_->saveTz((int16_t)tz)) {
        applyClock();
      }
    }

    const int minutes = server_.arg("interval").toInt();
    if (minutes >= 1 && minutes <= 240) {
      nvm_->saveInterval((uint16_t)minutes);
    }
    const int beep = server_.arg("beep").toInt();
    if (beep >= 0 && beep <= BEEP_LONG) {
      nvm_->saveBeep((uint8_t)beep);
    }

    const String mac = server_.arg("mac");
    if (mac.length() == 0) {
      nvm_->clearBleMac();
    } else {
      uint8_t bytes[6];
      if (parseMac(mac.c_str(), bytes)) {
        nvm_->saveBleMac(bytes);
      }
    }

    bool wifiChanged = false;
    if (server_.hasArg("clear_wifi")) {
      nvm_->clearNets();
      wifiChanged = true;
    } else if (server_.arg("ssid").length() > 0) {
      const String pass = server_.arg("pass");
      wifiChanged = nvm_->saveNet(server_.arg("ssid").c_str(), pass.c_str());
    }

    const int behavior = server_.arg("ap").toInt();
    if (behavior >= 0 && behavior <= BOTTLE_AP_TEMPORARY) {
      nvm_->saveAp(data.apSsid, data.apPass, (uint8_t)behavior);
    }

    if (server_.hasArg("js")) {
      armReply();
      server_.send(200, "text/plain", "ok");
    } else {
      server_.sendHeader("Connection", "close");
      server_.sendHeader("Location", "/", true);
      server_.send(302, "text/plain", "");
    }
    if (wifiChanged) {
      restartAtMs_ = millis() + 500;
    }
  }

  void sendStatus() {
    char body[896];
    writeStatus(body, sizeof(body));
    armReply();
    server_.send(200, "application/json", body);
  }

  const char *macText() const {
    static char text[18];
    if (!nvm_->data.hasBleMac) {
      text[0] = '\0';
      return text;
    }
    sprintf(text, "%02x:%02x:%02x:%02x:%02x:%02x", nvm_->data.bleMac[0], nvm_->data.bleMac[1], nvm_->data.bleMac[2],
            nvm_->data.bleMac[3], nvm_->data.bleMac[4], nvm_->data.bleMac[5]);
    return text;
  }

  static bool parseMac(const char *text, uint8_t out[6]) {
    unsigned int b[6];
    if (sscanf(text, "%x:%x:%x:%x:%x:%x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) {
      return false;
    }
    for (int i = 0; i < 6; i++) {
      if (b[i] > 255) {
        return false;
      }
      out[i] = (uint8_t)b[i];
    }
    return true;
  }
};
