#pragma once

// Active phone check. A short Bluetooth Low Energy connection to the paired
// address means the phone is here. No reply for ten seconds means away.
// The address is the printed order, the same order stored at pairing.

#include <Arduino.h>
#include <host/ble_gap.h>

#include <string.h>

class PhonePresence {
 public:
  static const uint32_t PING_MS = 2000;
  static const uint32_t AWAY_MS = 10000;

  void setAddress(const uint8_t mac[6]) {
    memcpy(mac_, mac, 6);
    hasMac_ = true;
    startedMs_ = millis();
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

  // An existing link counts as a reply. Otherwise one address type is tried
  // per call, so the bottle loop is not blocked for the whole timeout.
  void tick(bool linkUp) {
    if (!hasMac_ || suspended_) {
      return;
    }
    const uint32_t now = millis();
    if (linkUp) {
      markReply(now);
      cancel();
      return;
    }
    if (phase_ != PHASE_IDLE) {
      if (pingDone_) {
        const bool reply = pingStatus_ == 0;
        pingDone_ = false;
        phase_ = PHASE_IDLE;
        if (reply) {
          markReply(now);
        } else if (type_ == BLE_ADDR_PUBLIC) {
          start(BLE_ADDR_RANDOM, now);
        }
        return;
      }
      if (now - attemptMs_ > PING_MS + 300) {
        ble_gap_conn_cancel();
        phase_ = PHASE_IDLE;
        if (type_ == BLE_ADDR_PUBLIC) {
          start(BLE_ADDR_RANDOM, now);
        }
      }
      return;
    }
    if (attemptMs_ != 0 && now - attemptMs_ < 500) {
      return;
    }
    start(BLE_ADDR_PUBLIC, now);
  }

  bool here() const {
    if (!hasMac_) {
      return true;
    }
    const uint32_t now = millis();
    if (!ever_ && now - startedMs_ < AWAY_MS) {
      return true;
    }
    return ever_ && now - lastReplyMs_ < AWAY_MS;
  }

 private:
  static const uint8_t PHASE_IDLE = 0;
  static const uint8_t PHASE_WAIT = 1;

  uint8_t mac_[6] = {};
  bool hasMac_ = false;
  bool suspended_ = false;
  bool ever_ = false;
  uint8_t phase_ = PHASE_IDLE;
  uint8_t type_ = BLE_ADDR_PUBLIC;
  uint32_t startedMs_ = 0;
  uint32_t attemptMs_ = 0;
  uint32_t lastReplyMs_ = 0;
  volatile int pingStatus_ = -1;
  volatile bool pingDone_ = false;

  void markReply(uint32_t now) {
    lastReplyMs_ = now;
    ever_ = true;
  }

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
};
