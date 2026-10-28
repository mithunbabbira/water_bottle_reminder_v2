#pragma once

#include <Arduino.h>

static const int PIN_DT = 4;
static const int PIN_SCK = 3;
static const int PIN_WHITE = 1;
static const int PIN_BUZZER = 10;
static const int PIN_R = 5;
static const int PIN_G = 6;
static const int PIN_B = 7;

static const float MISSING_GRAMS = 80.0f;
static const float PRESENT_GRAMS = 88.0f;
static const float DRINK_GRAMS = 50.0f;
static const float REFILL_GRAMS = 100.0f;
static const float CONFIRM_DRIFT_GRAMS = 5.0f;

static const int CONFIRM_SAMPLES = 3;
static const uint32_t SAMPLE_MS = 120;
static const uint32_t CONFIRM_GAP_MS = 250;
static const uint32_t STABILIZE_MS = 2000;
static const uint32_t MISSING_ALERT_MS = 180000;
static const uint32_t BEEP_DELAY_MS = 5000;
static const uint32_t BLINK_MS = 500;
static const uint32_t WARMUP_MS = 1500;
static const uint32_t SAMPLE_MAX_AGE_MS = 400;
static const uint32_t UNREADABLE_RETRY_MS = 5000;

static const uint8_t QUIET_R = 0;
static const uint8_t QUIET_G = 0;
static const uint8_t QUIET_B = 255;
static const uint8_t ALERT_R = 0;
static const uint8_t ALERT_G = 255;
static const uint8_t ALERT_B = 255;
static const uint8_t MISSING_COLOR_R = 255;
static const uint8_t MISSING_COLOR_G = 0;
static const uint8_t MISSING_COLOR_B = 0;

struct Clock {
  bool valid = false;
  int hour = 0;
  int minute = 0;
  int32_t day = 0;
};

struct AtHome {
  bool watching = false;
  bool decided = false;
  bool home = false;
};
