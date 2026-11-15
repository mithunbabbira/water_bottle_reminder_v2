#pragma once

#include "BottleNvm.h"
#include "Config.h"

// bogde HX711 0.7.5, vendored in src/. This is the library hydration_v3 used.
// The sketch must not include the Rob Tillaart library.
#include "src/HX711.h"

// Channel A, gain 128. On ESP32 the data pin is INPUT (no pull-up).
// Boot never writes tare_offset. Zero is saved only from an explicit tare.
// One conversion per poll, then the same formula as get_units(1):
// grams = (raw - offset) / scale

class Scale {
 public:
  void begin() { scale_.begin(PIN_DT, PIN_SCK); }

  void poll(const BottleNvmData &nvm) {
    if (!scale_.is_ready()) {
      return;
    }
    applyNvm(nvm);
    const long raw = scale_.read();
    raw_ = (int32_t)raw;
    rawMs_ = millis();
    haveRaw_ = true;
    ++sequence_;
    fault_ = railed(raw_);
    if (collecting_ && !fault_) {
      if (collected_ == 0) minRaw_ = maxRaw_ = raw_;
      if (raw_ < minRaw_) minRaw_ = raw_;
      if (raw_ > maxRaw_) maxRaw_ = raw_;
      sum_ += raw_;
      ++collected_;
    }
    if (fault_ || !nvm.hasTare || !nvm.hasCal || scale_.get_scale() == 0.0f) {
      haveGrams_ = false;
      return;
    }
    grams_ = (float)((double)raw - (double)scale_.get_offset()) / scale_.get_scale();
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

  uint32_t sequence() const { return sequence_; }

  bool startOperation(float knownGrams = 0) {
    if (collecting_ || !isfinite(knownGrams) || knownGrams < 0 || knownGrams > 10000) return false;
    collecting_ = true; knownGrams_ = knownGrams;
    operationMs_ = millis(); collected_ = 0; sum_ = 0;
    haveGrams_ = false;
    return true;
  }
  bool busy() const { return collecting_; }
  // 0 pending, 1 saved, -1 failed; all sampling stays cooperative.
  int finishOperation(BottleNvm &nvm) {
    if (!collecting_) return 0;
    if (fault_ || millis() - operationMs_ >= 4000) { collecting_ = false; return -1; }
    if (collected_ < 10) return 0;
    collecting_ = false;
    const int32_t average = int32_t(sum_ / collected_);
    const float spread = float(maxRaw_ - minRaw_);
    haveGrams_ = false;
    if (knownGrams_ == 0) {
      // Fresh units have no grams conversion yet. Bound raw noise regardless;
      // calibrated units additionally require <= 5g peak-to-peak movement.
      if (spread > 1000 || (nvm.data.hasCal && spread > fabsf(nvm.data.calFactor) * 5.0f)) return -1;
      return nvm.saveTare(average) ? 1 : -1;
    }
    const float displacement = fabsf(float(average - nvm.data.tareOffset));
    const float factor = float(average - nvm.data.tareOffset) / knownGrams_;
    // Assess the candidate, not a potentially incorrect old factor. Require
    // >=100:1 signal/noise and <=1g movement (or 1% of a light reference).
    if (!nvm.data.hasTare || !isfinite(factor) || fabsf(factor) < 0.0001f ||
        displacement < 1000 || displacement < spread * 100 ||
        spread / fabsf(factor) > max(1.0f, knownGrams_ * 0.01f)) return -1;
    return nvm.saveCal(factor) ? 1 : -1;
  }

  bool tare(BottleNvm &nvm) {
    if (!scale_.wait_ready_retry(15, 20)) {
      return false;
    }
    scale_.tare(10);
    const int32_t offset = (int32_t)scale_.get_offset();
    if (railed(offset)) {
      fault_ = true;
      return false;
    }
    raw_ = offset;
    rawMs_ = millis();
    haveRaw_ = true;
    haveGrams_ = false;
    return nvm.saveTare(offset);
  }

  bool calibrate(BottleNvm &nvm, float knownGrams) {
    if (!nvm.data.hasTare || knownGrams <= 0.0f) {
      return false;
    }
    scale_.set_offset(nvm.data.tareOffset);
    if (!scale_.wait_ready_retry(15, 20)) {
      return false;
    }
    const long raw = scale_.read_average(10);
    const int32_t rawI = (int32_t)raw;
    if (railed(rawI)) {
      fault_ = true;
      return false;
    }
    const float factor = ((float)raw - (float)nvm.data.tareOffset) / knownGrams;
    if (factor == 0.0f) {
      return false;
    }
    raw_ = rawI;
    rawMs_ = millis();
    haveRaw_ = true;
    haveGrams_ = false;
    return nvm.saveCal(factor);
  }

 private:
  HX711 scale_;
  uint32_t sequence_ = 0, operationMs_ = 0;
  bool collecting_ = false;
  float knownGrams_ = 0;
  int64_t sum_ = 0;
  int32_t minRaw_ = 0, maxRaw_ = 0;
  uint16_t collected_ = 0;
  int32_t raw_ = 0;
  float grams_ = 0.0f;
  uint32_t rawMs_ = 0;
  uint32_t gramsMs_ = 0;
  bool haveRaw_ = false;
  bool haveGrams_ = false;
  bool fault_ = false;

  static bool railed(int32_t raw) { return raw <= -8388000 || raw >= 8388000; }

  void applyNvm(const BottleNvmData &nvm) {
    if (nvm.hasTare) {
      scale_.set_offset(nvm.tareOffset);
    }
    if (nvm.hasCal && nvm.calFactor != 0.0f) {
      scale_.set_scale(nvm.calFactor);
    }
  }
};
