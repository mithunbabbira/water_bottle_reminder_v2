#pragma once
#include "Arduino.h"
#include <vector>
#define WIFI_STA 1
#define WIFI_OFF 0
#define WIFI_SCAN_RUNNING -1
#define WIFI_SCAN_FAILED -2
#define WIFI_AUTH_OPEN 0
struct FakeWifiNetwork { String ssid; int rssi; int auth; };
struct FakeWifi {
  std::vector<FakeWifiNetwork> found;
  int result = WIFI_SCAN_RUNNING, beginResult = WIFI_SCAN_RUNNING, modeValue = -1;
  int deletes = 0, stops = 0, scans = 0;
  bool persistentValue = true, autoReconnect = true;
  void persistent(bool v) { persistentValue = v; }
  void mode(int v) { modeValue = v; }
  void setAutoReconnect(bool v) { autoReconnect = v; }
  void scanDelete() { ++deletes; }
  int scanNetworks(bool async, bool, bool, int) { if (!async) abort(); ++scans; return beginResult; }
  int scanComplete() { return result; }
  String SSID(int i) { return found.at(i).ssid; }
  int RSSI(int i) { return found.at(i).rssi; }
  int encryptionType(int i) { return found.at(i).auth; }
  void disconnect(bool, bool) {}
};
inline FakeWifi WiFi;
