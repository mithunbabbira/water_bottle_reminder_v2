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

  // Cycles the RGB LED. Call often while Wi-Fi is still coming up.
  void rainbow(uint32_t speedMs) {
    const uint32_t now = millis();
    if (now - rainbowMs_ < speedMs) {
      return;
    }
    rainbowMs_ = now;
    hue_ = (hue_ + 1) % 360;
    const float h = hue_ / 60.0f;
    const int band = (int)h;
    const float frac = h - band;
    const int down = (int)(255 * (1.0f - frac));
    const int up = (int)(255 * frac);
    uint8_t r = 0, g = 0, b = 0;
    switch (band) {
      case 0: r = 255; g = up; break;
      case 1: r = down; g = 255; break;
      case 2: g = 255; b = up; break;
      case 3: g = down; b = 255; break;
      case 4: r = up; b = 255; break;
      default: r = 255; b = down; break;
    }
    rgb(r, g, b);
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
  uint32_t rainbowMs_ = 0;
  int hue_ = 0;
  bool sounding_ = false;

  void rgb(uint8_t r, uint8_t g, uint8_t b) const {
    analogWrite(PIN_R, 255 - r);
    analogWrite(PIN_G, 255 - g);
    analogWrite(PIN_B, 255 - b);
  }

  void white(bool on) const { digitalWrite(PIN_WHITE, on ? HIGH : LOW); }
  void buzzer(bool on) const { digitalWrite(PIN_BUZZER, on ? HIGH : LOW); }
};
