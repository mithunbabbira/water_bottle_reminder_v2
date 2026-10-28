#pragma once

#include "Outputs.h"
#include "Scale.h"

// Boot does not tare, does not delete baseline_weight, and does not judge a drink.
// Baseline changes only when a drink or refill is confirmed, or the user tares.

enum class WeighResult { Unreadable, Waiting, NoChange, NewBaseline, Drink, Refill };

class Hydration {
 public:
  void begin(const BottleNvmData &nvm) {
    hasBaseline_ = nvm.hasBaseline;
    baseline_ = nvm.baselineWeight;
    dailyMl_ = nvm.dailyTotalMl;
    lastDay_ = nvm.lastDay;
    warmupUntil_ = millis() + WARMUP_MS;
  }

  void forgetBaseline(BottleNvm &nvm) {
    hasBaseline_ = false;
    baseline_ = 0.0f;
    nvm.clearBaseline();
    drinkAlert_ = false;
    resumeAlert_ = false;
  }

  void tick(uint32_t now, Scale &scale, BottleNvm &nvm, Outputs &out, const Clock &clock, const AtHome &atHome) {
    rollDay(clock, nvm);
    const bool away = atHome.watching && atHome.decided && !atHome.home;
    if (away && (drinkAlert_ || missingAlert_ || resumeAlert_)) {
      drinkAlert_ = false;
      missingAlert_ = false;
      resumeAlert_ = false;
      Serial.println("you left, reminder off");
    }
    if (away && missing_) {
      missingMs_ = now;
    }

    float grams = 0.0f;
    const bool fresh = scale.latestGrams(&grams, SAMPLE_MAX_AGE_MS);
    const bool quiet = inQuiet(nvm.data, clock);

    if (now < warmupUntil_ || !fresh) {
      paint(now, nvm, out, quiet);
      return;
    }

    const bool wasReady = bootReady_;
    trackPresence(now, grams);
    if (!wasReady && bootReady_) {
      onBootReady(now, grams, nvm);
    }
    if (!bootReady_) {
      paint(now, nvm, out, quiet);
      return;
    }

    if (stabilizing_) {
      if (now - stabilizeMs_ >= STABILIZE_MS) {
        stabilizing_ = false;
        const WeighResult result = weigh(scale, nvm);
        if (result == WeighResult::NoChange && resumeAlert_ && !quiet && phoneAllows(atHome)) {
          startAlert(now);
        }
        resumeAlert_ = false;
      }
    } else if (!missing_ && !drinkAlert_ && !quiet && (int32_t)(now - nextCheckMs_) >= 0) {
      const WeighResult result = weigh(scale, nvm);
      if (result == WeighResult::NoChange) {
        if (!phoneAllows(atHome)) {
          if (atHome.watching && !atHome.decided) {
            nextCheckMs_ = now + UNREADABLE_RETRY_MS;
          } else {
            Serial.println("away, reminder skipped");
          }
        } else if (!quiet) {
          startAlert(now);
        }
      }
    }

    paint(now, nvm, out, quiet);
  }

  float baseline() const { return baseline_; }
  bool hasBaseline() const { return hasBaseline_; }
  float dailyMl() const { return dailyMl_; }
  bool missing() const { return missing_; }
  bool alerting() const { return drinkAlert_ || missingAlert_; }
  bool hasLastDrink() const { return hasLastDrink_; }
  float lastDrinkMl() const { return lastDrinkMl_; }
  void setPump(void (*pump)()) { pump_ = pump; }

 private:
  bool hasBaseline_ = false;
  float baseline_ = 0.0f;
  float dailyMl_ = 0.0f;
  float lastDrinkMl_ = 0.0f;
  bool hasLastDrink_ = false;
  int32_t lastDay_ = 0;

  bool bootReady_ = false;
  bool missing_ = false;
  bool stabilizing_ = false;
  bool drinkAlert_ = false;
  bool resumeAlert_ = false;
  bool missingAlert_ = false;
  bool blinkOn_ = false;

  int missingCount_ = 0;
  int presentCount_ = 0;
  uint32_t sampleMs_ = 0;
  uint32_t missingMs_ = 0;
  uint32_t stabilizeMs_ = 0;
  uint32_t alertMs_ = 0;
  uint32_t blinkMs_ = 0;
  uint32_t nextCheckMs_ = 0;
  uint32_t warmupUntil_ = 0;
  void (*pump_)() = nullptr;

  void pause(uint32_t ms) {
    const uint32_t start = millis();
    while (millis() - start < ms) {
      if (pump_ != nullptr) {
        pump_();
      } else {
        delay(5);
      }
    }
  }

  void onBootReady(uint32_t now, float grams, BottleNvm &nvm) {
    if (missing_) {
      hasBaseline_ = false;
      baseline_ = 0.0f;
      nvm.clearBaseline();
      Serial.println("boot: bottle missing, baseline cleared");
      return;
    }
    if (!hasBaseline_) {
      nextCheckMs_ = now;
      Serial.println("boot: bottle present, learning baseline");
      return;
    }
    if (fabsf(grams - baseline_) >= REFILL_GRAMS) {
      adopt(nvm, grams);
      nextCheckMs_ = now + intervalMs(nvm);
      Serial.print("boot: baseline rebased ");
      Serial.println(baseline_, 1);
      return;
    }
    nextCheckMs_ = now + intervalMs(nvm);
    Serial.print("boot: baseline kept ");
    Serial.println(baseline_, 1);
  }

  void trackPresence(uint32_t now, float grams) {
    if (now - sampleMs_ < SAMPLE_MS) {
      return;
    }
    sampleMs_ = now;

    if (!bootReady_) {
      if (grams < MISSING_GRAMS) {
        presentCount_ = 0;
        if (++missingCount_ >= CONFIRM_SAMPLES) {
          noteLifted(now);
        }
      } else {
        missingCount_ = 0;
        if (++presentCount_ >= CONFIRM_SAMPLES) {
          missing_ = false;
          bootReady_ = true;
          presentCount_ = 0;
          missingCount_ = 0;
        }
      }
      return;
    }

    if (!missing_) {
      if (grams < MISSING_GRAMS) {
        presentCount_ = 0;
        if (++missingCount_ >= CONFIRM_SAMPLES) {
          noteLifted(now);
        }
      } else {
        missingCount_ = 0;
      }
      return;
    }

    if (grams > PRESENT_GRAMS) {
      missingCount_ = 0;
      if (++presentCount_ >= CONFIRM_SAMPLES) {
        noteReturned(now);
      }
    } else {
      presentCount_ = 0;
    }
  }

  void noteLifted(uint32_t now) {
    missing_ = true;
    missingMs_ = now;
    missingAlert_ = false;
    stabilizing_ = false;
    missingCount_ = 0;
    presentCount_ = 0;
    bootReady_ = true;
    if (drinkAlert_) {
      resumeAlert_ = true;
      drinkAlert_ = false;
    }
    Serial.println("bottle lifted");
  }

  void noteReturned(uint32_t now) {
    missing_ = false;
    missingAlert_ = false;
    stabilizing_ = true;
    stabilizeMs_ = now;
    missingCount_ = 0;
    presentCount_ = 0;
    bootReady_ = true;
    Serial.println("bottle returned");
  }

  WeighResult weigh(Scale &scale, BottleNvm &nvm) {
    float grams = 0.0f;
    if (!confirm(scale, nvm, &grams)) {
      nextCheckMs_ = millis() + UNREADABLE_RETRY_MS;
      Serial.println("weight unreadable, retry in 5s");
      return WeighResult::Unreadable;
    }
    if (!hasBaseline_) {
      if (grams < PRESENT_GRAMS) {
        nextCheckMs_ = millis() + 1000;
        return WeighResult::Waiting;
      }
      adopt(nvm, grams);
      nextCheckMs_ = millis() + intervalMs(nvm);
      Serial.print("baseline set ");
      Serial.println(baseline_, 1);
      return WeighResult::NewBaseline;
    }

    const float diff = baseline_ - grams;
    Serial.print("baseline ");
    Serial.print(baseline_, 1);
    Serial.print(" now ");
    Serial.print(grams, 1);
    Serial.print(" diff ");
    Serial.println(diff, 1);

    if (diff >= DRINK_GRAMS) {
      dailyMl_ += diff;
      lastDrinkMl_ = diff;
      hasLastDrink_ = true;
      adopt(nvm, grams);
      nvm.saveTotals(dailyMl_, lastDay_);
      drinkAlert_ = false;
      resumeAlert_ = false;
      nextCheckMs_ = millis() + intervalMs(nvm);
      Serial.print("drink ");
      Serial.print(diff, 1);
      Serial.print(" ml, today ");
      Serial.println(dailyMl_, 1);
      return WeighResult::Drink;
    }
    if (diff <= -REFILL_GRAMS) {
      adopt(nvm, grams);
      drinkAlert_ = false;
      resumeAlert_ = false;
      nextCheckMs_ = millis() + intervalMs(nvm);
      Serial.println("refill, baseline moved");
      return WeighResult::Refill;
    }
    nextCheckMs_ = millis() + intervalMs(nvm);
    return WeighResult::NoChange;
  }

  bool confirm(Scale &scale, BottleNvm &nvm, float *grams) {
    float first = 0.0f;
    float second = 0.0f;
    if (!scale.waitGrams(nvm.data, &first, 300, pump_)) {
      return false;
    }
    pause(CONFIRM_GAP_MS);
    if (!scale.waitGrams(nvm.data, &second, 300, pump_)) {
      return false;
    }
    if (fabsf(first - second) <= CONFIRM_DRIFT_GRAMS) {
      *grams = (first + second) * 0.5f;
      return true;
    }
    pause(CONFIRM_GAP_MS);
    float third = 0.0f;
    if (!scale.waitGrams(nvm.data, &third, 300, pump_)) {
      return false;
    }
    *grams = median(first, second, third);
    return true;
  }

  void adopt(BottleNvm &nvm, float grams) {
    baseline_ = grams;
    hasBaseline_ = true;
    nvm.saveBaseline(grams);
  }

  void startAlert(uint32_t now) {
    drinkAlert_ = true;
    alertMs_ = now;
    blinkMs_ = now;
    blinkOn_ = false;
    Serial.println("reminder");
  }

  void rollDay(const Clock &clock, BottleNvm &nvm) {
    if (!clock.valid || clock.day == lastDay_) {
      return;
    }
    if (lastDay_ != 0) {
      dailyMl_ = 0.0f;
      Serial.println("new day, total cleared");
    }
    lastDay_ = clock.day;
    nvm.saveTotals(dailyMl_, lastDay_);
  }

  void paint(uint32_t now, const BottleNvm &nvm, Outputs &out, bool quiet) {
    const bool beepAllowed = !nvm.data.dnd && !quiet;
    if (missing_ && now - missingMs_ >= MISSING_ALERT_MS) {
      if (!missingAlert_) {
        missingAlert_ = true;
        alertMs_ = now;
        blinkMs_ = now;
        Serial.println("missing alert");
      }
      const bool on = blink(now);
      const bool beep = beepAllowed && !on && now - alertMs_ >= BEEP_DELAY_MS;
      if (on) {
        out.show(MISSING_COLOR_R, MISSING_COLOR_G, MISSING_COLOR_B, true, beep, nvm.data.beepType);
      } else {
        out.show(0, 0, 0, false, beep, nvm.data.beepType);
      }
      return;
    }

    if (stabilizing_) {
      out.show(quiet ? QUIET_R : nvm.data.colorR, quiet ? QUIET_G : nvm.data.colorG,
               quiet ? QUIET_B : nvm.data.colorB, false, false, nvm.data.beepType);
      return;
    }

    if (drinkAlert_ && !quiet) {
      const bool on = blink(now);
      const bool beep = beepAllowed && !on && now - alertMs_ >= BEEP_DELAY_MS;
      if (on) {
        out.show(ALERT_R, ALERT_G, ALERT_B, true, beep, nvm.data.beepType);
      } else {
        out.show(0, 0, 0, false, beep, nvm.data.beepType);
      }
      return;
    }

    out.show(quiet ? QUIET_R : nvm.data.colorR, quiet ? QUIET_G : nvm.data.colorG,
             quiet ? QUIET_B : nvm.data.colorB, false, false, nvm.data.beepType);
  }

  bool blink(uint32_t now) {
    if (now - blinkMs_ >= BLINK_MS) {
      blinkMs_ = now;
      blinkOn_ = !blinkOn_;
    }
    return blinkOn_;
  }

  static bool phoneAllows(const AtHome &atHome) {
    if (!atHome.watching) {
      return true;
    }
    return atHome.decided && atHome.home;
  }

  static uint32_t intervalMs(const BottleNvm &nvm) {
    const uint16_t minutes = nvm.data.intervalMin == 0 ? 30 : nvm.data.intervalMin;
    return (uint32_t)minutes * 60000UL;
  }

  static bool inQuiet(const BottleNvmData &nvm, const Clock &clock) {
    if (!clock.valid || nvm.startMin == nvm.endMin) {
      return false;
    }
    const uint16_t nowMin = (uint16_t)(clock.hour * 60 + clock.minute);
    if (nvm.startMin < nvm.endMin) {
      return nowMin >= nvm.startMin && nowMin < nvm.endMin;
    }
    return nowMin >= nvm.startMin || nowMin < nvm.endMin;
  }

  static float median(float a, float b, float c) {
    if ((a <= b && b <= c) || (c <= b && b <= a)) {
      return b;
    }
    if ((b <= a && a <= c) || (c <= a && a <= b)) {
      return a;
    }
    return c;
  }
};
