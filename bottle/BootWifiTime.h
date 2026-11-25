#pragma once

#include "BottleNvm.h"
#include "ClockService.h"
#include <WiFi.h>
#include <esp_sntp.h>

// Cooperative Wi-Fi/SNTP state machine. No network waits in setup(), loop(), or
// a BLE callback. The app stays reachable even with a mistyped password.
// After a failed boot/manual sync, auto-retries with backoff while the clock is
// still invalid (Wi-Fi returns to OFF between attempts so Classic presence can run).
class BootWifiTime {
 public:
  void begin(BottleNvm &nvm, ClockService &clock) {
    if (clock.adoptSystemIfValid()) {
      source_ = "rtc";
    } else if (nvm.data.netCount > 0) {
      start(nvm);
    }
  }

  bool start(const BottleNvm &nvm, bool fromAutoRetry = false) {
    if (busy()) return false;
    count_ = nvm.data.netCount;
    for (uint8_t i = 0; i < count_; ++i) nets_[i] = nvm.data.nets[i];
    error_ = "";
    if (count_ == 0) {
      state_ = "no_networks";
      error_ = "Add a 2.4 GHz Wi-Fi network first.";
      return false;
    }
    if (!fromAutoRetry) {
      autoRetries_ = 0;
      nextRetryMs_ = kFirstRetryMs;
    }
    index_ = 0;
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);
    connectNext();
    return true;
  }

  void tick(ClockService &clock, BottleNvm &nvm) {
    if (!busy()) {
      maybeAutoRetry(clock, nvm);
      return;
    }
    const uint32_t now = millis();
    if (strcmp(state_, "connecting") == 0) {
      if (WiFi.status() == WL_CONNECTED) {
        // configTime(0,0,...) overwrites TZ. Preserve the configured local zone.
        char zone[40];
        snprintf(zone, sizeof(zone), "%s", getenv("TZ") ? getenv("TZ") : "UTC0");
        esp_sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
        configTzTime(zone, "pool.ntp.org", "time.google.com");
        state_ = "syncing";
        since_ = now;
      } else if (now - since_ >= 15000) {
        ++index_;
        if (index_ < count_) connectNext();
        else finish(false, "Cannot connect. Check 2.4 GHz Wi-Fi name and password.");
      }
    } else if (strcmp(state_, "syncing") == 0) {
      // Require an actual SNTP result, not a previously set app/system clock.
      if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED && clock.adoptSystemIfValid()) {
        source_ = "ntp";
        finish(true, "");
      } else if (now - since_ >= 15000) {
        finish(false, "Wi-Fi connected but internet time was unavailable. Retry or use app time.");
      }
    }
  }

  bool busy() const { return strcmp(state_, "connecting") == 0 || strcmp(state_, "syncing") == 0; }
  const char *state() const { return state_; }
  const char *error() const { return error_; }
  const char *ssid() const { return ssid_; }
  const char *source() const { return source_; }
  void appTimeSet() { source_ = "app"; }

 private:
  static const uint32_t kFirstRetryMs = 30000;
  static const uint32_t kMaxRetryMs = 300000;  // 5 minutes between later attempts
  BottleNetwork nets_[BOTTLE_MAX_NETWORKS] = {};
  uint8_t count_ = 0, index_ = 0, autoRetries_ = 0;
  uint32_t since_ = 0, failedAt_ = 0, nextRetryMs_ = kFirstRetryMs;
  const char *state_ = "idle";
  const char *error_ = "";
  const char *source_ = "unset";
  char ssid_[BOTTLE_SSID_LEN] = {};

  void connectNext() {
    WiFi.disconnect(false, false);
    strncpy(ssid_, nets_[index_].ssid, sizeof(ssid_) - 1);
    WiFi.begin(nets_[index_].ssid, nets_[index_].pass);
    since_ = millis();
    state_ = "connecting";
  }

  void finish(bool ok, const char *error) {
    esp_sntp_stop();
    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF);
    // Do not retain another copy of Wi-Fi passwords in RAM after sync.
    memset(nets_, 0, sizeof(nets_));
    state_ = ok ? "synced" : "failed";
    error_ = error;
    if (ok) {
      autoRetries_ = 0;
      nextRetryMs_ = kFirstRetryMs;
      return;
    }
    failedAt_ = millis();
    // 30s → 60s → 120s → 240s → then every 5 minutes while the clock stays invalid.
    if (autoRetries_ < 4) nextRetryMs_ = kFirstRetryMs << autoRetries_;
    else nextRetryMs_ = kMaxRetryMs;
    if (autoRetries_ < 255) ++autoRetries_;
  }

  void maybeAutoRetry(ClockService &clock, BottleNvm &nvm) {
    if (strcmp(state_, "failed") != 0) return;
    if (clock.valid()) return;  // app/RTC already usable — stop hammering Wi-Fi
    if (nvm.data.netCount == 0) return;
    if (uint32_t(millis() - failedAt_) < nextRetryMs_) return;
    start(nvm, true);
  }
};
