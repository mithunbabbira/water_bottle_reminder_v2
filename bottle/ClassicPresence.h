#pragma once
#include <Arduino.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_gap_bt_api.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <string.h>
#include <atomic>
#include "ClassicText.h"

// One process-lifetime driver, initialized AFTER BottleBle brings up dual-mode
// Bluedroid. GAP callbacks only copy bounded values into FreeRTOS queues. All
// policy/state mutation happens in tick() on the Arduino loop task.
class ClassicPresence {
 public:
  struct Device { uint8_t address[6]; char name[49]; int rssi; };
  ClassicPresence() = default;
  ClassicPresence(const ClassicPresence &) = delete;
  ClassicPresence &operator=(const ClassicPresence &) = delete;

  bool begin() {
    if (initialized_) return true;
    if (owner() && owner() != this) return false;
    if (esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_ENABLED ||
        esp_bluedroid_get_status() != ESP_BLUEDROID_STATUS_ENABLED) {
      error_ = "Bluetooth unavailable; restart bottle"; state_ = "radio_error"; return false;
    }
    if (!results_) results_ = xQueueCreate(16, sizeof(Message));
    if (!control_) control_ = xQueueCreate(4, sizeof(Message));
    if (!results_ || !control_) { error_ = "Bluetooth queue allocation failed"; state_ = "radio_error"; return false; }
    owner() = this;
    if (esp_bt_gap_register_callback(callback) != ESP_OK) {
      owner() = nullptr; error_ = "Classic callback unavailable; restart bottle"; state_ = "radio_error"; return false;
    }
    initialized_ = true; error_ = ""; state_ = configured_ ? "unknown" : "not_configured";
    return true;
  }
  void setTarget(const uint8_t *mac) {
    configured_ = mac != nullptr;
    if (mac) memcpy(target_, mac, 6); else memset(target_, 0, 6);
    positive_ = checked_ = probePresent_ = false;
    probeFinished_ = false;
    // An identical re-selection is still a new authorization identity.
    if (operation_ == Probe) invalidated_ = true;
    state_ = configured_ ? "unknown" : "not_configured"; error_ = "";
  }
  bool busy() const { return operation_ != Idle; }
  bool startScan(int id, bool fromBle, uint32_t session) {
    if (!initialized_ || busy()) return false;
    drain(); scanFinished_ = false; scanCount_ = 0; scanTruncated_ = false; scanError_ = "";
    scanId_ = id; scanFromBle_ = fromBle; scanSession_ = session;
    positive_ = false; operation_ = Scan; started_ = millis(); expired_ = false;
    scanState_ = "scanning"; state_ = "scanning"; error_ = "";
    if (esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 8, 0) != ESP_OK) {
      operation_ = Idle; scanState_ = "failed"; scanError_ = "radio_error";
      state_ = "radio_error"; error_ = "Discovery could not start; retry"; return false;
    }
    return true;
  }
  bool startProbe(int id, bool fromBle, uint32_t session, bool automatic = false) {
    if (!initialized_ || busy() || !configured_) return false;
    drain(); probeFinished_ = false; probeId_ = id; probeFromBle_ = fromBle;
    probeSession_ = session; automatic_ = automatic; probePresent_ = positive_ = false;
    probeReason_ = "radio_error"; probeElapsed_ = 0;
    memcpy(requestTarget_, target_, 6); invalidated_ = expired_ = false;
    operation_ = Probe; started_ = millis(); state_ = "checking"; error_ = "";
    if (esp_bt_gap_read_remote_name(requestTarget_) != ESP_OK) {
      operation_ = Idle; finishProbe(millis(), false, "radio_error");
      probeFinished_ = false; // Rejected start gets a negative ACK, not an async result.
      return false;
    }
    return true;
  }
  void tick(uint32_t now) {
    if (!initialized_) return;
    if (dropped_.exchange(false) && operation_ == Scan) scanTruncated_ = true;
    // Deadline is applied BEFORE draining, so a queued late completion can never
    // turn a timeout into success merely because loop scheduling was delayed.
    if (busy() && !expired_ && now - started_ >= 20000) {
      expired_ = true;
      if (operation_ == Probe && !invalidated_) finishProbe(now, false, "timeout");
      if (operation_ == Scan) {
        scanFinished_ = true; scanState_ = "failed"; scanError_ = "timeout";
        esp_bt_gap_cancel_discovery();
      }
      state_ = "stalled"; error_ = "Bluetooth completion pending; restart bottle if it persists";
    }
    Message m;
    // Discovery queue is separate: a crowded room cannot discard completions.
    for (unsigned n = 0; n < 16 && xQueueReceive(results_, &m, 0) == pdTRUE; ++n) {
      if (operation_ == Scan && !expired_) addDevice(m.device);
    }
    for (unsigned n = 0; n < 4 && xQueueReceive(control_, &m, 0) == pdTRUE; ++n) {
      if (operation_ == Probe && m.kind == Name && !memcmp(m.device.address, requestTarget_, 6)) {
        operation_ = Idle;
        if (!expired_ && !invalidated_) finishProbe(now, m.success, m.success ? "reply" : "no_reply");
        else { positive_ = false; state_ = configured_ ? (expired_ ? "timeout" : "unknown") : "not_configured"; error_ = ""; }
      } else if (operation_ == Scan && m.kind == Stopped) {
        operation_ = Idle;
        if (!expired_) { scanFinished_ = true; scanState_ = "complete"; }
        state_ = configured_ ? "unknown" : "not_configured"; error_ = "";
      }
    }
    if (positive_ && now - checkedMs_ >= 30000) { positive_ = false; state_ = "unknown"; }
  }
  bool takeScanFinished() { const bool v = scanFinished_; scanFinished_ = false; return v; }
  int scanId() const { return scanId_; }
  bool scanFromBle() const { return scanFromBle_; }
  uint32_t scanSession() const { return scanSession_; }
  uint8_t scanCount() const { return scanCount_; }
  bool scanTruncated() const { return scanTruncated_; }
  const char *scanError() const { return scanError_; }
  const char *scanState() const { return scanState_; }
  const Device &device(size_t i) const { static const Device empty = {}; return i < scanCount_ ? devices_[i] : empty; }
  bool takeProbeFinished() { const bool v = probeFinished_; probeFinished_ = false; return v; }
  int probeId() const { return probeId_; }
  bool probeFromBle() const { return probeFromBle_; }
  uint32_t probeSession() const { return probeSession_; }
  bool probeAutomatic() const { return automatic_; }
  bool probePresent() const { return probePresent_; }
  const char *probeReason() const { return probeReason_; }
  uint32_t probeElapsedMs() const { return probeElapsed_; }
  const char *state() const { return state_; }
  const char *error() const { return error_; }
  bool here(uint32_t now) const { return configured_ && positive_ && now - checkedMs_ < 30000; }
  bool hasLastCheck() const { return checked_; }
  uint32_t lastCheckAgeSeconds(uint32_t now) const { return (now - checkedMs_) / 1000; }

 private:
  enum Operation { Idle, Scan, Probe };
  enum Kind { Found, Name, Stopped };
  struct Message { Kind kind; Device device; bool success; };
  static ClassicPresence *&owner() { static ClassicPresence *p = nullptr; return p; }
  static void callback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *p) {
    ClassicPresence *self = owner();
    if (!self || !p) return;
    Message m = {};
    if (event == ESP_BT_GAP_READ_REMOTE_NAME_EVT) {
      m.kind = Name; memcpy(m.device.address, p->read_rmt_name.bda, 6);
      m.success = p->read_rmt_name.stat == ESP_BT_STATUS_SUCCESS;
    } else if (event == ESP_BT_GAP_DISC_STATE_CHANGED_EVT && p->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
      m.kind = Stopped;
    } else if (event == ESP_BT_GAP_DISC_RES_EVT) {
      m.kind = Found; memcpy(m.device.address, p->disc_res.bda, 6); m.device.rssi = -127;
      for (int i = 0; i < p->disc_res.num_prop; ++i) {
        const auto &prop = p->disc_res.prop[i];
        if (!prop.val || prop.len <= 0) continue;
        const uint8_t *b = static_cast<const uint8_t *>(prop.val);
        if (prop.type == ESP_BT_GAP_DEV_PROP_RSSI) m.device.rssi = static_cast<int8_t>(*b);
        if (prop.type == ESP_BT_GAP_DEV_PROP_BDNAME) classicNameCopy(m.device.name, b, prop.len);
        if (prop.type == ESP_BT_GAP_DEV_PROP_EIR && !m.device.name[0]) {
          // Parse using the actual bounded property length, not an assumed 240B.
          for (size_t pos = 0; pos < static_cast<size_t>(prop.len) && b[pos];) {
            const size_t n = b[pos];
            if (n < 1 || pos + n + 1 > static_cast<size_t>(prop.len)) break;
            if (b[pos+1] == ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME || b[pos+1] == ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME)
              classicNameCopy(m.device.name, b + pos + 2, n - 1);
            pos += n + 1;
          }
        }
      }
      if (xQueueSend(self->results_, &m, 0) != pdTRUE) self->dropped_.store(true);
      return;
    } else return;
    // If this bounded queue ever overflows, timeout quarantines fail-closed.
    xQueueSend(self->control_, &m, 0);
  }
  void drain() {
    Message m;
    for (unsigned n = 0; n < 16 && xQueueReceive(results_, &m, 0) == pdTRUE; ++n) {}
    for (unsigned n = 0; n < 4 && xQueueReceive(control_, &m, 0) == pdTRUE; ++n) {}
    dropped_.store(false);
  }
  void finishProbe(uint32_t now, bool present, const char *reason) {
    checked_ = true; checkedMs_ = now; probeElapsed_ = now - started_;
    probePresent_ = positive_ = present; probeReason_ = reason; probeFinished_ = true;
    state_ = present ? "present" : !strcmp(reason, "timeout") ? "timeout" : !strcmp(reason, "radio_error") ? "radio_error" : "away";
    error_ = present ? "" : "Selected phone did not reply; check Bluetooth and retry";
  }
  void addDevice(const Device &d) {
    uint8_t i = 0;
    while (i < scanCount_ && memcmp(devices_[i].address, d.address, 6)) ++i;
    if (i < scanCount_) {
      if (d.rssi > devices_[i].rssi) devices_[i].rssi = d.rssi;
      if (d.name[0]) memcpy(devices_[i].name, d.name, 49);
    } else if (scanCount_ < 8) devices_[scanCount_++] = d;
    else {
      scanTruncated_ = true;
      if (d.rssi > devices_[7].rssi) devices_[7] = d;
    }
    for (uint8_t j = 1; j < scanCount_; ++j) {
      Device v = devices_[j]; uint8_t k = j;
      while (k && devices_[k-1].rssi < v.rssi) { devices_[k] = devices_[k-1]; --k; }
      devices_[k] = v;
    }
  }
  std::atomic<bool> dropped_{false};
  QueueHandle_t results_ = nullptr, control_ = nullptr;
  bool initialized_ = false, configured_ = false, positive_ = false, checked_ = false;
  bool expired_ = false, invalidated_ = false, scanFinished_ = false, probeFinished_ = false;
  Operation operation_ = Idle;
  uint8_t target_[6] = {}, requestTarget_[6] = {};
  uint32_t started_ = 0, checkedMs_ = 0;
  const char *state_ = "not_configured", *error_ = "";
  int scanId_ = 0, probeId_ = 0;
  bool scanFromBle_ = false, probeFromBle_ = false, automatic_ = false;
  uint32_t scanSession_ = 0, probeSession_ = 0, probeElapsed_ = 0;
  bool probePresent_ = false, scanTruncated_ = false;
  const char *probeReason_ = "no_reply", *scanError_ = "", *scanState_ = "idle";
  uint8_t scanCount_ = 0;
  Device devices_[8] = {};
};
