#pragma once

#include "BottleNvm.h"
#include "Config.h"

#include <sys/time.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

// System time is always UTC; civil time is a fixed minutes-east-of-UTC offset.
// Conversions deliberately do NOT depend on the process TZ environment, which
// ESP configTime/configTzTime may replace later during asynchronous SNTP setup.
class ClockService {
 public:
  static const time_t kMinSaneEpoch = 1700000000L;

  const Clock &clock() const { return clock_; }
  void begin(int16_t tzMin) {
    tzMin_ = validTz(tzMin) ? tzMin : 330;
    applyTzEnv();
  }
  void setTz(int16_t tzMin) {
    if (!validTz(tzMin)) return;
    tzMin_ = tzMin; applyTzEnv();
    if (valid_) refreshFromRtc();
  }
  int16_t tzMin() const { return tzMin_; }
  bool valid() const { return valid_; }
  const char *source() const { return source_; }
  const char *timeSource() const { return source_; }
  time_t epoch() const { return valid_ ? time(nullptr) : 0; }

  // Pass "ntp" only on a confirmed SNTP callback/completion, not merely because
  // a sane RTC or an app-set clock exists while an NTP request is pending.
  bool adoptSystemIfValid(const char *source = "rtc") {
    if (time(nullptr) < kMinSaneEpoch) return false;
    valid_ = true;
    if (source && strcmp(source, "ntp") == 0) source_ = "ntp";
    else if (strcmp(source_, "unset") == 0) source_ = "rtc";
    refreshFromRtc();
    return valid_;
  }
  bool setEpoch(time_t epochUtc, const char *source = "app") {
    if (!atEpoch(epochUtc, tzMin_).valid) return false;
    timeval tv = {}; tv.tv_sec = epochUtc;
    if (settimeofday(&tv, nullptr) != 0) return false;
    valid_ = true;
    source_ = source && strcmp(source, "ntp") == 0 ? "ntp" : "app";
    refreshFromRtc();
    return true;
  }
  bool setLocal(int year, int month, int day, int hour, int minute, int second = 0) {
    time_t utc = 0;
    return localToEpoch(year, month, day, hour, minute, second, tzMin_, &utc) && setEpoch(utc);
  }
  void tick() { if (valid_) refreshFromRtc(); }

  static bool validTz(int16_t tz) { return tz >= -720 && tz <= 840 && tz % 15 == 0; }
  static bool inActiveWindow(const BottleNvmData &nvm, const Clock &clock) {
    if (!clock.valid || clock.hour < 0 || clock.hour > 23 || clock.minute < 0 || clock.minute > 59 ||
        nvm.startMin >= 1440 || nvm.endMin >= 1440 || nvm.startMin == nvm.endMin) return false;
    const uint16_t minute = static_cast<uint16_t>(clock.hour * 60 + clock.minute);
    return nvm.startMin < nvm.endMin ? minute >= nvm.startMin && minute < nvm.endMin :
                                     minute >= nvm.startMin || minute < nvm.endMin;
  }
  static bool outsideActive(const BottleNvmData &nvm, const Clock &clock) { return !inActiveWindow(nvm, clock); }

  // Public deterministic helpers also support native tests without changing the
  // host clock. Sub-hour signs must not be lost: +00:30 => POSIX UTC-0:30.
  static bool tzString(int16_t tzMin, char *out, size_t size) {
    if (!validTz(tzMin) || out == nullptr || size == 0) return false;
    const int absolute = abs(static_cast<int>(tzMin));
    const int n = snprintf(out, size, "UTC%c%d:%02d", tzMin > 0 ? '-' : '+', absolute / 60, absolute % 60);
    return n >= 0 && static_cast<size_t>(n) < size;
  }
  static Clock atEpoch(time_t utc, int16_t tzMin) {
    Clock c;
    if (utc < kMinSaneEpoch || static_cast<int64_t>(utc) > 253402300799LL || !validTz(tzMin)) return c;
    const int64_t shifted = static_cast<int64_t>(utc) + static_cast<int64_t>(tzMin) * 60;
    const time_t localEpoch = static_cast<time_t>(shifted);
    if (static_cast<int64_t>(localEpoch) != shifted) return c;
    struct tm local = {};
    if (gmtime_r(&localEpoch, &local) == nullptr || local.tm_year < 123 || local.tm_year > 8099) return c;
    c.valid = true; c.hour = local.tm_hour; c.minute = local.tm_min;
    c.day = (local.tm_year + 1900) * 10000 + (local.tm_mon + 1) * 100 + local.tm_mday;
    return c;
  }
  static bool localToEpoch(int year, int month, int day, int hour, int minute, int second,
                           int16_t tzMin, time_t *result) {
    if (!result || year < 2024 || year > 2099 || month < 1 || month > 12 || !validTz(tzMin) ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) return false;
    static const uint8_t days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    const int maxDay = days[month - 1] + (month == 2 && leap ? 1 : 0);
    if (day < 1 || day > maxDay) return false;
    // Gregorian civil date -> days since 1970-01-01 (no mktime/TZ side effects).
    int y = year - (month <= 2 ? 1 : 0);
    const int era = y / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned mp = static_cast<unsigned>(month + (month > 2 ? -3 : 9));
    const unsigned doy = (153 * mp + 2) / 5 + static_cast<unsigned>(day) - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const int64_t daysUtc = static_cast<int64_t>(era) * 146097 + doe - 719468;
    const int64_t seconds = daysUtc * 86400 + hour * 3600 + minute * 60 + second - static_cast<int64_t>(tzMin) * 60;
    const time_t epoch = static_cast<time_t>(seconds);
    if (static_cast<int64_t>(epoch) != seconds || epoch < kMinSaneEpoch) return false;
    *result = epoch;
    return true;
  }

 private:
  Clock clock_;
  bool valid_ = false;
  int16_t tzMin_ = 330;
  const char *source_ = "unset";

  void applyTzEnv() {
    char buf[32];
    if (tzString(tzMin_, buf, sizeof(buf))) { setenv("TZ", buf, 1); tzset(); }
  }
  void refreshFromRtc() {
    clock_ = atEpoch(time(nullptr), tzMin_);
    if (!clock_.valid) { valid_ = false; source_ = "unset"; }
  }
};
