#pragma once

#include "ClockService.h"
#include "HydrationEngine.h"
#include "Outputs.h"
#include "Scale.h"

// Thin ESP adapter. Scale is polled by the main loop; tick never polls/waits,
// probes a phone, delays, or calls a network API. sequence() identifies actual
// conversions, so a cached value cannot confirm presence or a drink repeatedly.
class Hydration {
 public:
  void begin(const BottleNvmData &nvm) {
    engine_.begin(millis(), nvm.hasBaseline, nvm.baselineWeight, nvm.dailyTotalMl, nvm.lastDay);
    testIntervalSec_ = 0;
    lastPersistAttemptMs_ = 0;
    attemptedPersist_ = false;
    persistenceFailed_ = false;
  }

  void tick(uint32_t now, Scale &scale, BottleNvm &nvm, Outputs &out, const Clock &clock, bool phoneHere) {
    HydrationSettings settings;
    settings.intervalMs = testIntervalSec_ ? static_cast<uint32_t>(testIntervalSec_) * 1000UL :
                                          static_cast<uint32_t>(nvm.data.intervalMin) * 60000UL;
    settings.missingMs = static_cast<uint32_t>(nvm.data.missingSec) * 1000UL;
    settings.beepDelayMs = static_cast<uint32_t>(nvm.data.beepDelaySec) * 1000UL;
    settings.drinkGrams = nvm.data.drinkGrams;
    settings.settleMs = nvm.data.settleMs;
    settings.idleColor = (static_cast<uint32_t>(nvm.data.colorR) << 16) |
                         (static_cast<uint32_t>(nvm.data.colorG) << 8) | nvm.data.colorB;
    settings.reminderColor = nvm.data.reminderColor;
    settings.missingColor = nvm.data.missingColor;
    settings.beepType = nvm.data.beepType;
    settings.dnd = nvm.data.dnd;
    settings.presenceEnabled = nvm.data.presenceEnabled;
    HydrationInput input;
    input.scaleValid = nvm.data.hasTare && nvm.data.hasCal && !scale.busy() &&
                       scale.latestGrams(&input.grams, SAMPLE_MAX_AGE_MS);
    input.scaleFault = scale.fault();
    input.sequence = scale.sequence();
    input.clockValid = clock.valid;
    input.activeWindow = ClockService::inActiveWindow(nvm.data, clock);
    input.day = clock.day;
    input.phoneHere = phoneHere;
    engine_.tick(now, settings, input);
    const HydrationFrame &frame = engine_.frame();
    // Silence outputs BEFORE attempting a flash write. A failed write is retried
    // at most once every 5 s, not every fast main-loop iteration.
    out.show((frame.color >> 16) & 255, (frame.color >> 8) & 255, frame.color & 255,
             frame.white, frame.beep, settings.beepType);
    persist(now, nvm);
  }

  // Call after a successful explicit tare/calibration. Clears ALL transient
  // alarm/presence/settling state and starts warmup again; never auto-tares.
  bool forgetBaseline(BottleNvm &nvm) {
    engine_.resetAfterTare(millis());
    attemptedPersist_ = false;
    return persist(millis(), nvm);
  }
  bool clearToday(BottleNvm &nvm) {
    engine_.clearToday(); attemptedPersist_ = false;
    return persist(millis(), nvm);
  }
  void setCheckSeconds(uint16_t seconds, const BottleNvm &nvm) {
    testIntervalSec_ = seconds;
    engine_.restartInterval(millis(), seconds ? static_cast<uint32_t>(seconds) * 1000UL :
                                              static_cast<uint32_t>(nvm.data.intervalMin) * 60000UL);
  }
  void useSavedInterval(const BottleNvm &nvm) {
    testIntervalSec_ = 0;
    engine_.restartInterval(millis(), static_cast<uint32_t>(nvm.data.intervalMin) * 60000UL);
  }
  // Kept for older setup code; the engine deliberately never invokes a pump.
  void setPump(void (*)()) {}

  float baseline() const { return engine_.baseline(); }
  bool hasBaseline() const { return engine_.hasBaseline(); }
  float dailyMl() const { return engine_.dailyMl(); }
  bool missing() const { return engine_.missing(); }
  bool alerting() const { return engine_.alerting(); }
  bool hasLastDrink() const { return engine_.hasLastDrink(); }
  float lastDrinkMl() const { return engine_.lastDrinkMl(); }
  bool ready() const { return engine_.ready(); }
  bool scaleError() const { return engine_.scaleError(); }
  const char *stateName() const { return engine_.stateName(); }
  uint32_t nextCheckSeconds(uint32_t now) const { return engine_.nextCheckSeconds(now); }
  bool persistenceFailed() const { return persistenceFailed_; }
  bool storageFault() const { return persistenceFailed_; }
  bool persistencePending() const { return engine_.persistenceDirty(); }
  // Bitmask of HydrationEngine::Event, retained until consumed. Several events
  // can occur in one tick (e.g. return/no-change/reminder). Drink ml is lastDrinkMl().
  uint16_t takeEvents() { return engine_.takeEvents(); }

 private:
  HydrationEngine engine_;
  uint16_t testIntervalSec_ = 0;
  uint32_t lastPersistAttemptMs_ = 0;
  bool attemptedPersist_ = false, persistenceFailed_ = false;

  bool persist(uint32_t now, BottleNvm &nvm) {
    if (!engine_.persistenceDirty()) return true;
    if (attemptedPersist_ && now - lastPersistAttemptMs_ < UNREADABLE_RETRY_MS) return false;
    attemptedPersist_ = true; lastPersistAttemptMs_ = now;
    const bool ok = nvm.saveHydration(engine_.hasBaseline(), engine_.baseline(), engine_.dailyMl(), engine_.lastDay());
    persistenceFailed_ = !ok;
    if (ok) { engine_.persisted(); attemptedPersist_ = false; }
    return ok;
  }
};
