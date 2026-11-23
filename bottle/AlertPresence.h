#pragma once
#include <stdint.h>

// Authorization for an alert episode, separate from UI/manual reachability.
// A manual "test phone" response must never pre-authorize a future reminder.
class AlertPresence {
 public:
  void update(uint32_t now, bool needsPresence) {
    if (needsPresence != active_) {
      invalidate(); active_ = needsPresence;
    }
    if (granted_ && uint32_t(now - replyMs_) >= 30000) granted_ = false;
  }
  void invalidate() { ++epoch_; granted_ = pending_ = attempted_ = false; }
  bool shouldProbe(uint32_t now) const {
    return active_ && !pending_ && (!attempted_ || uint32_t(now - attemptMs_) >= 15000);
  }
  uint32_t epoch() const { return epoch_; }
  void started(uint32_t now) { pending_ = attempted_ = true; attemptMs_ = now; }
  void complete(uint32_t token, bool present, uint32_t now) {
    if (token != epoch_ || !pending_ || !active_) return;
    pending_ = false; granted_ = present; replyMs_ = now;
  }
  bool allowed(uint32_t now) const { return active_ && granted_ && uint32_t(now - replyMs_) < 30000; }
 private:
  uint32_t epoch_ = 1, attemptMs_ = 0, replyMs_ = 0;
  bool active_ = false, pending_ = false, attempted_ = false, granted_ = false;
};
