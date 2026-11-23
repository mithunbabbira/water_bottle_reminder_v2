#pragma once
#include <stdint.h>

// A MAC address is useful diagnostics, not proof that an ordinary phone is
// nearby. Phones are BLE centrals and rotate private addresses. Do not block the
// hydration loop attempting to connect to a phone that is not advertising.
// Mandatory presence requires the app link, with disconnect grace and no boot grace.
class PhonePresence {
 public:
  void connected(uint32_t now) { linked_ = true; seen_ = true; lastSeen_ = now; }
  void disconnected(uint32_t now) { linked_ = false; lastSeen_ = now; }
  bool here(uint32_t now) const {
    return linked_ || (seen_ && uint32_t(now - lastSeen_) < 60000UL);
  }
 private:
  bool linked_ = false, seen_ = false;
  uint32_t lastSeen_ = 0;
};
