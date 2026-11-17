#pragma once

#include <cstdint>

namespace watch {

struct WatchSettings {
  bool airplane = false;
  bool vibration = true;
  bool motion_wake = true;
  uint8_t brightness = 100;
  uint8_t timeout_seconds = 10;
};

inline WatchSettings sanitizeSettings(WatchSettings value) {
  if (value.brightness > 100) value.brightness = 100;
  if (value.timeout_seconds != 5 && value.timeout_seconds != 10 &&
      value.timeout_seconds != 15 && value.timeout_seconds != 30) {
    value.timeout_seconds = 10;
  }
  return value;
}

inline bool operator==(const WatchSettings& a, const WatchSettings& b) {
  return a.airplane == b.airplane && a.vibration == b.vibration &&
      a.motion_wake == b.motion_wake && a.brightness == b.brightness &&
      a.timeout_seconds == b.timeout_seconds;
}

inline bool operator!=(const WatchSettings& a, const WatchSettings& b) {
  return !(a == b);
}

// User values 0..100 map linearly to 10..100 percent PWM, never screen-off.
inline uint16_t brightnessDuty(const WatchSettings& settings, uint16_t maxDuty = 1023) {
  const uint32_t value = settings.brightness > 100 ? 100 : settings.brightness;
  const uint16_t duty = static_cast<uint16_t>((uint32_t(maxDuty) * (100U + 9U * value) + 500U) / 1000U);
  return maxDuty && !duty ? 1 : duty;
}

inline uint32_t timeoutMillis(const WatchSettings& settings) {
  return uint32_t(sanitizeSettings(settings).timeout_seconds) * 1000U;
}

struct WatchDateTime {
  int year, month, day, hour, minute, second;
  WatchDateTime(int y = 2000, int mo = 1, int d = 1,
                int h = 0, int mi = 0, int s = 0)
      : year(y), month(mo), day(d), hour(h), minute(mi), second(s) {}
};

inline bool settingsLeapYear(int year) {
  return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

inline int settingsDaysInMonth(int year, int month) {
  static const uint8_t days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  if (month < 1 || month > 12) return 31;
  return days[month - 1] + (month == 2 && settingsLeapYear(year) ? 1 : 0);
}

inline bool validDateTime(const WatchDateTime& value) {
  return value.year >= 1 && value.year <= 9999 && value.month >= 1 &&
      value.month <= 12 && value.day >= 1 &&
      value.day <= settingsDaysInMonth(value.year, value.month) &&
      value.hour >= 0 && value.hour < 24 && value.minute >= 0 &&
      value.minute < 60 && value.second >= 0 && value.second < 60;
}

inline WatchDateTime sanitizeDateTime(WatchDateTime value) {
  if (value.year < 1) value.year = 1;
  if (value.year > 9999) value.year = 9999;
  if (value.month < 1) value.month = 1;
  if (value.month > 12) value.month = 12;
  if (value.day < 1) value.day = 1;
  const int last = settingsDaysInMonth(value.year, value.month);
  if (value.day > last) value.day = last;
  if (value.hour < 0) value.hour = 0;
  if (value.hour > 23) value.hour = 23;
  if (value.minute < 0) value.minute = 0;
  if (value.minute > 59) value.minute = 59;
  if (value.second < 0) value.second = 0;
  if (value.second > 59) value.second = 59;
  return value;
}

}  // namespace watch
