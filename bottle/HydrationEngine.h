#pragma once

// Hardware-free, single-threaded state machine. No Arduino, waits, callbacks or
// flash writes. Input sequence MUST identify a conversion, not a loop iteration.
#include <stdint.h>
#include <math.h>

struct HydrationSettings {
  uint32_t intervalMs = 1800000;
  uint32_t missingMs = 180000;
  uint32_t beepDelayMs = 5000;
  uint16_t drinkGrams = 50;
  uint16_t settleMs = 2000;
  uint32_t idleColor = 0x8000ffu;
  uint32_t reminderColor = 0x00ffffu;
  uint32_t missingColor = 0xff0000u;
  uint8_t beepType = 2;
  bool dnd = false;
};

struct HydrationInput {
  bool scaleValid = false;  // Caller must reject stale data and uncalibrated scale.
  bool scaleFault = false;
  uint32_t sequence = 0;
  float grams = 0;
  bool clockValid = false;
  bool activeWindow = false;
  int32_t day = 0;
  bool phoneHere = false; // Fresh automatic episode authorization, NEVER a BLE link.
};

struct HydrationFrame {
  uint32_t color = 0;
  bool white = false;
  bool beep = false;  // Enable pattern generation in Outputs, not a blocking tone.
};

class HydrationEngine {
 public:
  enum Event : uint16_t {
    Drink = 1u << 0, Refill = 1u << 1, Lifted = 1u << 2, Returned = 1u << 3,
    Reminder = 1u << 4, MissingAlert = 1u << 5, Paused = 1u << 6, ScaleError = 1u << 7
  };
  enum class State { Warming, Idle, Missing, Settling, Reminder, MissingAlert, Paused, ScaleError };

  void begin(uint32_t now, bool hasBaseline = false, float baseline = 0, float dailyMl = 0, int32_t day = 0) {
    *this = HydrationEngine();
    startedMs_ = now;
    hasBaseline_ = hasBaseline && isfinite(baseline) && baseline >= kMissingGrams;
    baseline_ = hasBaseline_ ? baseline : 0;
    dailyMl_ = isfinite(dailyMl) && dailyMl >= 0 ? dailyMl : 0;
    lastDay_ = day;
  }

  void tick(uint32_t now, const HydrationSettings &settings, const HydrationInput &in) {
    settings_ = settings;
    const uint32_t requestedInterval = settings.intervalMs ? settings.intervalMs : 1800000;
    if (intervalMs_ != requestedInterval) {
      intervalMs_ = requestedInterval;
      restartInterval(now);
    }
    rollDay(in);
    quiet_ = !in.clockValid || !in.activeWindow;
    away_ = !in.phoneHere; // mandatory for every alert; never bypassed by settings/NVS
    const bool allowed = !quiet_ && !away_;
    if (!allowed && (reminder_ || missingAlert_)) events_ |= Paused;
    if (!allowed) {
      reminder_ = false; missingAlert_ = false; // Drink feedback is not a presence-gated nag.
    }

    // A cached sample can keep the scale healthy, but can NEVER serve as another
    // presence or confirmation sample. Also enforce sequence freshness here, so
    // even a caller accidentally marking a stuck ADC valid fails closed.
    const bool readable = in.scaleValid && !in.scaleFault && isfinite(in.grams);
    const bool fresh = readable && (!haveSequence_ || in.sequence != sequence_);
    if (fresh) { sequence_ = in.sequence; haveSequence_ = true; lastFreshMs_ = now; }
    if (!readable || (haveSequence_ && elapsed(now, lastFreshMs_) > kSampleMaxAgeMs)) {
      if (!scaleError_) events_ |= ScaleError;
      scaleError_ = true;
      reminder_ = missingAlert_ = reminderDue_ = flash_ = false;
      confirming_ = settling_ = retry_ = false;
      lowCount_ = presentCount_ = 0;
      missingVerified_ = false;
      provisionalLift_ = false;
      recovered_ = true;
      frame_ = HydrationFrame();
      state_ = State::ScaleError;
      return;
    }
    scaleError_ = false;
    if (elapsed(now, startedMs_) < kWarmupMs) {
      frame_ = HydrationFrame();
      if (quiet_) frame_.color = kQuietColor;
      state_ = State::Warming;
      return;
    }
    if (recovered_) {
      recovered_ = false;
      // Never weigh a transient recovery sample against the baseline. Require a
      // new settle/confirmation cycle, while preserving the original baseline.
      if (bootReady_ && !missing_) startSettle(now, Reason::Recovery);
    }

    if (fresh) trackPresence(now, in.grams);

    if (bootReady_ && missing_) {
      if (allowed && missingVerified_ && !provisionalReturn_ && elapsed(now, missingMs_) >= settings.missingMs && !missingAlert_) {
        missingAlert_ = true; alertMs_ = now; events_ |= MissingAlert;
      }
    } else if (bootReady_ && !provisionalLift_) {
      if (settling_ && elapsed(now, settleMs_) >= settings.settleMs) {
        settling_ = false;
        startConfirm(now, settleReason_);
      }
      if (confirming_) {
        if (fresh) collect(now, in.grams);
        if (confirming_ && elapsed(now, confirmMs_) >= kConfirmTimeoutMs) {
          // Moving water has a bounded attempt, then a bounded 5-second retry.
          // A median of unrelated moving samples is NOT a stable measurement.
          confirming_ = false; retry_ = true; retryMs_ = now;
          reminder_ = missingAlert_ = false;
          sampleCount_ = 0;
        }
      } else if (!settling_ && ((retry_ && elapsed(now, retryMs_) >= kRetryMs) || (!retry_ && due(now, nextCheckMs_)))) {
        startConfirm(now, retry_ ? retryReason_ : Reason::Interval);
        retry_ = false;
        if (fresh) collect(now, in.grams);
      }
      if (allowed && reminderDue_ && hasBaseline_ && !settling_ && !confirming_ && !retry_ && !reminder_) {
        reminder_ = true; alertMs_ = now; events_ |= Reminder;
      }
    }
    paint(now, allowed);
  }

  // Called immediately after an interval save, including saving the same value.
  // Settings are sampled again on the next tick. Cancels an old due reminder.
  void restartInterval(uint32_t now, uint32_t intervalMs = 0) {
    if (intervalMs != 0) intervalMs_ = intervalMs;
    nextCheckMs_ = now + intervalMs_;
    reminder_ = reminderDue_ = false;
    if (confirming_ && retryReason_ == Reason::Interval) confirming_ = false;
    if (retry_ && retryReason_ == Reason::Interval) retry_ = false;
  }
  void resetAfterTare(uint32_t now) {
    const float total = dailyMl_;
    const int32_t day = lastDay_;
    begin(now, false, 0, total, day);
    persistenceDirty_ = true;
  }
  void clearToday() {
    dailyMl_ = lastDrinkMl_ = 0; hasLastDrink_ = false; persistenceDirty_ = true;
  }

  const HydrationFrame &frame() const { return frame_; }
  State state() const { return state_; }
  const char *stateName() const {
    switch (state_) {
      case State::Warming: return "warming";
      case State::Idle: return "idle";
      case State::Missing: return "missing";
      case State::Settling: return "settling";
      case State::Reminder: return "reminder";
      case State::MissingAlert: return "missing_alert";
      case State::Paused: return "paused";
      case State::ScaleError: return "scale_error";
    }
    return "scale_error";
  }
  bool ready() const { return bootReady_ && !scaleError_; }
  bool hasBaseline() const { return hasBaseline_; }
  float baseline() const { return baseline_; }
  float dailyMl() const { return dailyMl_; }
  int32_t lastDay() const { return lastDay_; }
  bool hasLastDrink() const { return hasLastDrink_; }
  float lastDrinkMl() const { return lastDrinkMl_; }
  bool missing() const { return missing_; }
  bool alerting() const { return reminder_ || missingAlert_; }
  // Independent of phoneHere: the orchestrator must discover due episodes while
  // outputs are suppressed, and obtain a NEW automatic result for that episode.
  bool needsPresence(uint32_t now) const {
    if (!bootReady_ || scaleError_ || quiet_ || !haveSequence_ ||
        elapsed(now, lastFreshMs_) > kSampleMaxAgeMs) return false;
    if (missing_) return missingVerified_ && !provisionalReturn_ &&
                         (missingAlert_ || elapsed(now, missingMs_) >= settings_.missingMs);
    return hasBaseline_ && !provisionalLift_ && !settling_ && !confirming_ && !retry_ &&
           (reminderDue_ || reminder_);
  }
  bool scaleError() const { return scaleError_; }
  bool persistenceDirty() const { return persistenceDirty_; }
  void persisted() { persistenceDirty_ = false; }
  uint16_t takeEvents() { const uint16_t e = events_; events_ = 0; return e; }
  uint32_t nextCheckSeconds(uint32_t now) const {
    if (!bootReady_) return 0;
    if (retry_) {
      const uint32_t age = elapsed(now, retryMs_);
      return age >= kRetryMs ? 0 : (kRetryMs - age + 999) / 1000;
    }
    return due(now, nextCheckMs_) ? 0 : (nextCheckMs_ - now + 999) / 1000;
  }

 private:
  enum class Reason { Boot, Return, Interval, Recovery };
  static const uint32_t kWarmupMs = 1500, kConfirmTimeoutMs = 5000, kRetryMs = 5000;
  static const uint32_t kConfirmSpanMs = 200, kConfirmSampleGapMs = 100;
  static const uint32_t kFlashMs = 2000, kBlinkMs = 500, kSampleMaxAgeMs = 400;
  static const uint32_t kQuietColor = 0x0000ffu, kDrinkColor = 0x00ff00u;
  static constexpr float kMissingGrams = 100, kPresentGrams = 108, kRefillGrams = 100, kStableGrams = 5;

  HydrationSettings settings_;
  HydrationFrame frame_;
  State state_ = State::Warming;
  uint32_t startedMs_ = 0, sequence_ = 0, lastFreshMs_ = 0, intervalMs_ = 1800000, nextCheckMs_ = 0;
  uint32_t missingMs_ = 0, settleMs_ = 0, confirmMs_ = 0, retryMs_ = 0, alertMs_ = 0, flashMs_ = 0;
  uint32_t sampleTimes_[3] = {};
  float samples_[3] = {};
  uint8_t sampleCount_ = 0, lowCount_ = 0, presentCount_ = 0;
  float baseline_ = 0, dailyMl_ = 0, lastDrinkMl_ = 0, bootDelta_ = 0;
  int32_t lastDay_ = 0;
  bool hasBaseline_ = false, hasLastDrink_ = false, persistenceDirty_ = false;
  bool haveSequence_ = false, bootReady_ = false, bootRebase_ = true, missing_ = false, missingVerified_ = false;
  bool provisionalLift_ = false, provisionalReturn_ = false, settling_ = false, confirming_ = false, retry_ = false;
  bool reminder_ = false, reminderDue_ = false, missingAlert_ = false, flash_ = false;
  bool quiet_ = true, away_ = false, scaleError_ = false, recovered_ = false;
  Reason settleReason_ = Reason::Boot, retryReason_ = Reason::Boot;
  uint16_t events_ = 0;

  static uint32_t elapsed(uint32_t now, uint32_t since) { return now - since; }
  static bool due(uint32_t now, uint32_t at) { return static_cast<int32_t>(now - at) >= 0; }

  void rollDay(const HydrationInput &in) {
    if (!in.clockValid || in.day == 0 || in.day == lastDay_) return;
    if (lastDay_ != 0) dailyMl_ = 0;
    lastDay_ = in.day; persistenceDirty_ = true;
  }
  void trackPresence(uint32_t now, float grams) {
    if (grams < kMissingGrams) {
      presentCount_ = 0; provisionalReturn_ = false;
      // Suppress both lights and sound at the FIRST low conversion, without
      // waiting for the second conversion used to confirm the lifted event.
      if (!missing_) {
        if (!provisionalLift_) missingMs_ = now;
        provisionalLift_ = true;
        reminderDue_ = reminderDue_ || reminder_;
        reminder_ = missingAlert_ = flash_ = false;
        confirming_ = settling_ = retry_ = false;
        if (++lowCount_ >= 2) {
          missing_ = true; missingVerified_ = true; bootReady_ = true; provisionalLift_ = false;
          lowCount_ = 0; events_ |= Lifted;
        }
      } else if (!missingVerified_ && ++lowCount_ >= 2) {
        // Requalify absence after a scale fault before resuming its alarm.
        missingVerified_ = true; lowCount_ = 0;
      }
      return;
    }
    lowCount_ = 0;
    if (provisionalLift_) {
      provisionalLift_ = false;
      startSettle(now, bootRebase_ ? Reason::Boot : Reason::Return);
    }
    if (missing_) {
      // Do not keep alarming through the return debounce.
      missingAlert_ = false;
      provisionalReturn_ = true;
      if (grams >= kPresentGrams) {
        if (++presentCount_ >= 3) {
          missing_ = false; provisionalReturn_ = false; presentCount_ = 0;
          startSettle(now, bootRebase_ ? Reason::Boot : Reason::Return);
          events_ |= Returned;
        }
      } else presentCount_ = 0;
      return;
    }
    if (!bootReady_) {
      if (grams >= kPresentGrams) {
        if (++presentCount_ >= 3) {
          bootReady_ = true; presentCount_ = 0;
          startConfirm(now, Reason::Boot);
        }
      } else presentCount_ = 0;
    }
  }
  void startSettle(uint32_t now, Reason reason) {
    settling_ = true; settleMs_ = now; settleReason_ = reason;
    confirming_ = retry_ = false;
    reminder_ = missingAlert_ = flash_ = false;
    sampleCount_ = 0;
  }
  void startConfirm(uint32_t now, Reason reason) {
    confirming_ = true; confirmMs_ = now; retryReason_ = reason;
    sampleCount_ = 0;
  }
  void collect(uint32_t now, float grams) {
    // In particular, a sub-100 g missing-base reading must NEVER be counted as
    // water consumed, including during a return or recovery confirmation.
    if (grams < kMissingGrams) return;
    // Decimate fast (80 Hz) ADC streams. Without this gap a sliding three-sample
    // window would never span 200 ms and could fail confirmation forever.
    if (sampleCount_ && elapsed(now, sampleTimes_[sampleCount_ - 1]) < kConfirmSampleGapMs) return;
    if (sampleCount_ == 3) {
      samples_[0] = samples_[1]; samples_[1] = samples_[2];
      sampleTimes_[0] = sampleTimes_[1]; sampleTimes_[1] = sampleTimes_[2];
      sampleCount_ = 2;
    }
    samples_[sampleCount_] = grams; sampleTimes_[sampleCount_++] = now;
    if (sampleCount_ < 3 || elapsed(now, sampleTimes_[0]) < kConfirmSpanMs) return;
    float lo = samples_[0], hi = samples_[0];
    for (uint8_t i = 1; i < 3; ++i) {
      if (samples_[i] < lo) lo = samples_[i];
      if (samples_[i] > hi) hi = samples_[i];
    }
    if (hi - lo > kStableGrams) return;
    const float gramsStable = samples_[0] + (samples_[1] - samples_[0]) / 3 + (samples_[2] - samples_[0]) / 3;
    confirming_ = false;
    evaluate(now, gramsStable, retryReason_);
  }
  void adopt(float grams) {
    hasBaseline_ = true; baseline_ = grams; bootDelta_ = 0;
    persistenceDirty_ = true;
  }
  void evaluate(uint32_t now, float grams, Reason reason) {
    if (bootRebase_) {
      nextCheckMs_ = now + intervalMs_;
      // Offline changes are not drinks. A >=100 g change rebases the stored
      // baseline; a smaller boot difference is excluded from future intake via
      // bootDelta_, while preserving the saved baseline and small-sip reference.
      if (!hasBaseline_ || fabsf(baseline_ - grams) >= kRefillGrams) adopt(grams);
      else bootDelta_ = baseline_ - grams;
      bootRebase_ = false; reminderDue_ = false;
      return;
    }
    if (!hasBaseline_) { adopt(grams); nextCheckMs_ = now + intervalMs_; reminderDue_ = false; return; }
    const float diff = baseline_ - bootDelta_ - grams;
    if (diff >= settings_.drinkGrams) {
      const float total = dailyMl_ + diff;
      if (!isfinite(total)) return;  // Corrupt/unrepresentable weights fail safe.
      dailyMl_ = total; lastDrinkMl_ = diff; hasLastDrink_ = true;
      adopt(grams); reminder_ = reminderDue_ = missingAlert_ = false;
      nextCheckMs_ = now + intervalMs_;
      flash_ = true; flashMs_ = now; events_ |= Drink;
    } else if (diff <= -kRefillGrams) {
      adopt(grams); reminder_ = reminderDue_ = missingAlert_ = flash_ = false;
      nextCheckMs_ = now + intervalMs_;
      events_ |= Refill;
    } else if (reason == Reason::Interval) {
      nextCheckMs_ = now + intervalMs_;
      reminderDue_ = true;
    }
    // An unchanged return or a sub-threshold sip must not postpone a scheduled
    // interval. Only a confirmed drink/refill earns a fresh full interval.
  }
  void paint(uint32_t now, bool allowed) {
    frame_ = HydrationFrame();
    if (!bootReady_) state_ = State::Warming;
    else if (!allowed) state_ = State::Paused;
    else if (missing_ && missingAlert_) state_ = State::MissingAlert;
    else if (missing_) state_ = State::Missing;
    else if (settling_ || confirming_) state_ = State::Settling;
    else if (reminder_) state_ = State::Reminder;
    else state_ = State::Idle;

    if (quiet_) { frame_.color = kQuietColor; return; }
    if (provisionalLift_ || provisionalReturn_) return;
    if (missing_ && !missingAlert_) return;
    // Physical drink confirmation remains visible even without alert permission.
    if (flash_ && elapsed(now, flashMs_) < kFlashMs) { frame_.color = kDrinkColor; return; }
    flash_ = false;
    if (!allowed) { frame_.color = settings_.idleColor; return; }
    if (reminder_ || missingAlert_) {
      const bool on = (elapsed(now, alertMs_) / kBlinkMs) % 2 == 0;
      frame_.color = on ? (missingAlert_ ? settings_.missingColor : settings_.reminderColor) : 0;
      frame_.white = on;
      frame_.beep = !on && !settings_.dnd && settings_.beepType != 0 && elapsed(now, alertMs_) >= settings_.beepDelayMs;
      return;
    }
    frame_.color = settings_.idleColor;
  }
};
