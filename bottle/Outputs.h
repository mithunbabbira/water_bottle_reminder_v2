#pragma once

#include "BottleNvm.h"
#include "Config.h"

// Common-anode RGB: a channel is lit by driving the pin low.
// White LED and buzzer are lit by driving the pin high.

class Outputs {
 public:
  void begin() {
    pinMode(PIN_WHITE, OUTPUT);
    pinMode(PIN_BUZZER, OUTPUT);
    pinMode(PIN_R, OUTPUT);
    pinMode(PIN_G, OUTPUT);
    pinMode(PIN_B, OUTPUT);
    rgb(0, 0, 0);
    white(false);
    buzzer(false);
  }

  void show(uint8_t r, uint8_t g, uint8_t b, bool whiteOn, bool beepOn, uint8_t beepType) {
    rgb(r, g, b);
    white(whiteOn);
    const uint32_t now = millis();
    if (!beepOn || beepType == BEEP_OFF) {
      buzzer(false);
      sounding_ = false;
      return;
    }
    if (beepType == BEEP_LONG) {
      buzzer(true);
      return;
    }
    if (!sounding_) {
      sounding_ = true;
      chirpMs_ = now;
    }
    uint32_t phase = now - chirpMs_;
    if (phase >= 520) {
      chirpMs_ = now;
      phase = 0;
    }
    if (beepType == BEEP_SINGLE) {
      buzzer(phase < 80);
      return;
    }
    // Two short chirps, then quiet. This is the beep-beep reminder.
    buzzer(phase < 80 || (phase >= 150 && phase < 230));
  }

 private:
  uint32_t chirpMs_ = 0;
  bool sounding_ = false;

  void rgb(uint8_t r, uint8_t g, uint8_t b) const {
    analogWrite(PIN_R, 255 - r);
    analogWrite(PIN_G, 255 - g);
    analogWrite(PIN_B, 255 - b);
  }

  void white(bool on) const { digitalWrite(PIN_WHITE, on ? HIGH : LOW); }
  void buzzer(bool on) const { digitalWrite(PIN_BUZZER, on ? HIGH : LOW); }
};
