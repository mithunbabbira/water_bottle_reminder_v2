#pragma once

#include "BottleNvm.h"
#include "Config.h"

// HX711 channel A, gain 128. A sample is used only while it is fresh.
// Boot never writes tare_offset. Zero is saved only from an explicit tare.

class Scale {
 public:
  void begin() {
    pinMode(PIN_SCK, OUTPUT);
    pinMode(PIN_DT, INPUT);
    digitalWrite(PIN_SCK, LOW);
    delay(10);
    digitalWrite(PIN_SCK, HIGH);
    delayMicroseconds(80);
    digitalWrite(PIN_SCK, LOW);
    if (waitLow(200)) {
      shift();
    }
  }

  void poll(const BottleNvmData &nvm) {
    if (digitalRead(PIN_DT) != LOW) {
      return;
    }
    raw_ = shift();
    rawMs_ = millis();
    haveRaw_ = true;
    fault_ = railed(raw_);
    if (fault_ || !nvm.hasTare || !nvm.hasCal) {
      haveGrams_ = false;
      return;
    }
    grams_ = (float)(raw_ - nvm.tareOffset) / nvm.calFactor;
    gramsMs_ = rawMs_;
    haveGrams_ = true;
  }

  bool latestRaw(int32_t *raw, uint32_t maxAgeMs) const {
    if (!haveRaw_ || millis() - rawMs_ > maxAgeMs) {
      return false;
    }
    *raw = raw_;
    return true;
  }

  // A stuck full-scale reading means a load-cell wire is open or shorted.
  bool fault() const { return haveRaw_ && fault_; }

  bool latestGrams(float *grams, uint32_t maxAgeMs) const {
    if (!haveGrams_ || millis() - gramsMs_ > maxAgeMs) {
      return false;
    }
    *grams = grams_;
    return true;
  }

  bool waitGrams(const BottleNvmData &nvm, float *grams, uint32_t timeoutMs, void (*pump)() = nullptr) {
    const uint32_t start = millis();
    while (millis() - start < timeoutMs) {
      poll(nvm);
      if (haveGrams_ && gramsMs_ >= start) {
        *grams = grams_;
        return true;
      }
      if (pump != nullptr) {
        pump();
      } else {
        delay(5);
      }
    }
    return false;
  }

  bool tare(BottleNvm &nvm) {
    int32_t raw = 0;
    if (!averageRaw(&raw, 10)) {
      return false;
    }
    return nvm.saveTare(raw);
  }

  bool calibrate(BottleNvm &nvm, float knownGrams) {
    if (!nvm.data.hasTare || knownGrams <= 0.0f) {
      return false;
    }
    int32_t raw = 0;
    if (!averageRaw(&raw, 10)) {
      return false;
    }
    const float factor = (float)(raw - nvm.data.tareOffset) / knownGrams;
    if (factor == 0.0f) {
      return false;
    }
    return nvm.saveCal(factor);
  }

 private:
  int32_t raw_ = 0;
  float grams_ = 0.0f;
  uint32_t rawMs_ = 0;
  uint32_t gramsMs_ = 0;
  bool haveRaw_ = false;
  bool haveGrams_ = false;
  bool fault_ = false;

  static bool railed(int32_t raw) { return raw <= -8388000 || raw >= 8388000; }

  bool waitLow(uint32_t timeoutMs) const {
    const uint32_t start = millis();
    while (digitalRead(PIN_DT) != LOW) {
      if (millis() - start > timeoutMs) {
        return false;
      }
      delay(1);
    }
    return true;
  }

  bool averageRaw(int32_t *raw, int samples) {
    int32_t sum = 0;
    int count = 0;
    for (int i = 0; i < samples; i++) {
      if (!waitLow(200)) {
        break;
      }
      sum += shift();
      count++;
    }
    if (count == 0) {
      return false;
    }
    *raw = sum / count;
    if (railed(*raw)) {
      fault_ = true;
      return false;
    }
    haveRaw_ = true;
    raw_ = *raw;
    rawMs_ = millis();
    haveGrams_ = false;
    return true;
  }

  int32_t shift() const {
    uint32_t value = 0;
    noInterrupts();
    for (int i = 0; i < 24; i++) {
      digitalWrite(PIN_SCK, HIGH);
      delayMicroseconds(1);
      value = (value << 1) | (uint32_t)digitalRead(PIN_DT);
      digitalWrite(PIN_SCK, LOW);
      delayMicroseconds(1);
    }
    digitalWrite(PIN_SCK, HIGH);
    delayMicroseconds(1);
    digitalWrite(PIN_SCK, LOW);
    interrupts();
    if (value & 0x800000UL) {
      value |= 0xFF000000UL;
    }
    return (int32_t)value;
  }
};
