#pragma once

// Phone check used only right before a reminder. A short BLE connection to the
// paired address means the phone is here. No periodic ping.

#include <Arduino.h>
#include <host/ble_gap.h>

#include "Config.h"

#include <string.h>

class PhonePresence {
 public:
  static const uint32_t PING_MS = 2000;

  void setAddress(const uint8_t mac[6]) {
    memcpy(mac_, mac, 6);
    hasMac_ = true;
  }

  bool configured() const { return hasMac_; }

  void suspend(bool on) {
    if (on == suspended_) {
      return;
    }
    suspended_ = on;
    if (on) {
      cancel();
    }
  }

  // Drop an in-flight ping so Wi-Fi can finish a page send on the shared radio.
  void holdRadio(bool on) {
    if (on == radioHeld_) {
      return;
    }
    if (on) {
      heldHere_ = lastHere_;
      cancel();
      delay(40);
    }
    radioHeld_ = on;
  }

  bool here() const {
    if (!hasMac_) {
      return true;
    }
    if (radioHeld_) {
      return heldHere_;
    }
    return lastHere_;
  }

  // One public then one random attempt. Pumps the scale while waiting.
  PhoneProbe probe(bool linkUp, void (*pump)()) {
    if (!hasMac_) {
      return PhoneProbe::Allow;
    }
    if (suspended_) {
      return PhoneProbe::Deny;
    }
    if (radioHeld_) {
      return PhoneProbe::Busy;
    }
    if (linkUp) {
      lastHere_ = true;
      return PhoneProbe::Allow;
    }
    Serial.println("phone check");
    if (tryOnce(BLE_ADDR_PUBLIC, pump) || tryOnce(BLE_ADDR_RANDOM, pump)) {
      lastHere_ = true;
      Serial.println("phone here");
      return PhoneProbe::Allow;
    }
    lastHere_ = false;
    Serial.println("phone away");
    return PhoneProbe::Deny;
  }

 private:
  static const uint8_t PHASE_IDLE = 0;
  static const uint8_t PHASE_WAIT = 1;

  uint8_t mac_[6] = {};
  bool hasMac_ = false;
  bool suspended_ = false;
  bool radioHeld_ = false;
  bool heldHere_ = true;
  bool lastHere_ = true;
  uint8_t phase_ = PHASE_IDLE;
  uint8_t type_ = BLE_ADDR_PUBLIC;
  uint32_t attemptMs_ = 0;
  volatile int pingStatus_ = -1;
  volatile bool pingDone_ = false;

  void cancel() {
    if (phase_ != PHASE_IDLE) {
      ble_gap_conn_cancel();
    }
    phase_ = PHASE_IDLE;
    pingDone_ = false;
  }

  static int onGap(struct ble_gap_event *event, void *arg) {
    PhonePresence *self = static_cast<PhonePresence *>(arg);
    if (event->type == BLE_GAP_EVENT_CONNECT) {
      self->pingStatus_ = event->connect.status;
      self->pingDone_ = true;
      if (event->connect.status == 0) {
        ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
      }
    }
    return 0;
  }

  void start(uint8_t addrType, uint32_t now) {
    ble_addr_t peer;
    for (int i = 0; i < 6; i++) {
      peer.val[i] = mac_[5 - i];
    }
    peer.type = addrType;
    pingDone_ = false;
    pingStatus_ = -1;
    type_ = addrType;
    attemptMs_ = now;
    const int rc = ble_gap_connect(BLE_OWN_ADDR_PUBLIC, &peer, PING_MS, nullptr, onGap, this);
    phase_ = rc == 0 ? PHASE_WAIT : PHASE_IDLE;
  }

  bool tryOnce(uint8_t addrType, void (*pump)()) {
    const uint32_t now = millis();
    start(addrType, now);
    if (phase_ != PHASE_WAIT) {
      return false;
    }
    const uint32_t deadline = now + PING_MS + 300;
    while ((int32_t)(millis() - deadline) < 0 && !pingDone_) {
      if (pump != nullptr) {
        pump();
      }
      delay(5);
    }
    const bool reply = pingDone_ && pingStatus_ == 0;
    if (!pingDone_) {
      ble_gap_conn_cancel();
    }
    phase_ = PHASE_IDLE;
    pingDone_ = false;
    return reply;
  }
};
