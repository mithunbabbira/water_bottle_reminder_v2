#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>

// Async STA scan shares the radio with BLE; never waits in the hydration loop.
class WifiDiscovery {
 public:
  static constexpr int kMaxNetworks = 15;
  struct Network { String ssid; int rssi; bool secured; };
  bool start(int id, bool fromBle, uint32_t session) {
    if (running_) return false;
    id_ = id; fromBle_ = fromBle; session_ = session;
    count_ = hidden_ = 0; truncated_ = false; error_ = ""; finished_ = false;
    WiFi.persistent(false); WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(false);
    WiFi.scanDelete(); since_ = millis(); running_ = true; state_ = "scanning";
    const int result = WiFi.scanNetworks(true, false, false, 120);
    if (result != WIFI_SCAN_RUNNING) complete(result);
    return true;
  }
  void tick() {
    if (!running_) return;
    const int result = WiFi.scanComplete();
    if (result >= 0 || result == WIFI_SCAN_FAILED) complete(result);
    else if (millis() - since_ >= 15000) {
      esp_wifi_scan_stop(); complete(WIFI_SCAN_FAILED);
    }
  }
  bool busy() const { return running_; }
  bool takeFinished() { const bool b = finished_; finished_ = false; return b; }
  int id() const { return id_; }
  bool fromBle() const { return fromBle_; }
  uint32_t session() const { return session_; }
  int count() const { return count_; }
  int hiddenCount() const { return hidden_; }
  bool truncated() const { return truncated_; }
  const char *error() const { return error_; }
  const char *state() const { return state_; }
  const Network &network(int i) const { return networks_[i]; }
 private:
  Network networks_[kMaxNetworks];
  bool running_ = false, finished_ = false, fromBle_ = false, truncated_ = false;
  int id_ = 0, count_ = 0, hidden_ = 0;
  uint32_t session_ = 0, since_ = 0;
  const char *error_ = "", *state_ = "idle";
  void complete(int result) {
    if (result < 0) error_ = "Network search failed. Move closer to the router and retry.";
    else for (int i = 0; i < result; ++i) {
      String name = WiFi.SSID(i);
      if (name.isEmpty()) { ++hidden_; continue; }
      const int rssi = WiFi.RSSI(i);
      bool duplicate = false;
      for (int j = 0; j < count_; ++j) if (networks_[j].ssid == name) {
        if (rssi > networks_[j].rssi) networks_[j] = {name, rssi, WiFi.encryptionType(i) != WIFI_AUTH_OPEN};
        duplicate = true; break;
      }
      if (duplicate) continue;
      if (count_ < kMaxNetworks) networks_[count_++] = {name, rssi, WiFi.encryptionType(i) != WIFI_AUTH_OPEN};
      else {
        truncated_ = true;
        int weakest = 0;
        for (int j = 1; j < count_; ++j) if (networks_[j].rssi < networks_[weakest].rssi) weakest = j;
        if (rssi > networks_[weakest].rssi) networks_[weakest] = {name, rssi, WiFi.encryptionType(i) != WIFI_AUTH_OPEN};
      }
    }
    for (int i = 0; i < count_; ++i) for (int j = i+1; j < count_; ++j)
      if (networks_[j].rssi > networks_[i].rssi) { Network tmp = networks_[i]; networks_[i] = networks_[j]; networks_[j] = tmp; }
    WiFi.scanDelete(); WiFi.disconnect(true, false); WiFi.mode(WIFI_OFF);
    running_ = false; finished_ = true; state_ = error_[0] ? "failed" : "complete";
  }
};
