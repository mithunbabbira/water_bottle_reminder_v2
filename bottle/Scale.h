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
    fault_ = railed(raw_);
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
