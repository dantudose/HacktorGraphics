#pragma once

#include <cstdint>
#include <limits>

namespace watch {

// Gregorian day ordinal, with 0001-01-01 = 0. Inputs are valid civil dates
// (years 1..9999), already in the same local calendar as the watch display.
inline int32_t civilDayNumber(int year, int month, int day) {
  static const int month_offsets[] = {
      0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
  const int32_t previous_year = year - 1;
  int32_t result = previous_year * 365 + previous_year / 4 -
                   previous_year / 100 + previous_year / 400 +
                   month_offsets[month - 1] + day - 1;
  const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
  if (month > 2 && leap) ++result;
  return result;
}

struct CivilDate {
  int year;
  int month;
  int day;
};

// Inverse of civilDayNumber for valid ordinals 0..3652058. Locate the year
// in at most 14 comparisons, then the month in at most 11 more; boot fallback
// can recover any persisted calendar date without walking every elapsed day.
inline CivilDate civilDateFromDayNumber(int32_t day) {
  int first_year = 1;
  int last_year = 9999;
  while (first_year < last_year) {
    const int middle = first_year + (last_year - first_year + 1) / 2;
    if (civilDayNumber(middle, 1, 1) <= day) first_year = middle;
    else last_year = middle - 1;
  }
  int month = 1;
  while (month < 12 && civilDayNumber(first_year, month + 1, 1) <= day) {
    ++month;
  }
  return {first_year, month,
          static_cast<int>(day - civilDayNumber(first_year, month, 1) + 1)};
}

// Clock milliseconds can include days since its current anchor. At exactly
// midnight the next boundary is a full day away; observe the current day first.
inline uint32_t millisUntilMidnight(uint64_t clock_ms) {
  return static_cast<uint32_t>(86400000ULL - clock_ms % 86400000ULL);
}

// Keep the sensor's free-running counter and maintain a separate daily total.
// A day change needs a successful raw snapshot at the boundary before counting
// the new day. No sample means no baseline was committed (e.g. an I2C failure).
// Fewer than 65536 steps must pass between samples to disambiguate sensor wraps.
class DailySteps {
 public:
  bool observeDay(int32_t day_key) {
    if (has_day_ && day_key == day_key_) return false;
    has_day_ = true;
    day_key_ = day_key;
    count_ = 0;
    needs_baseline_ = true;
    return true;
  }

  // Restore a saved total only after selecting its calendar date. The IMU may
  // have reset independently, so its next successful sample is a fresh raw
  // baseline; failed reads leave the restored total and pending baseline intact.
  void restoreCount(uint32_t count) {
    if (!has_day_) return;
    count_ = count;
    needs_baseline_ = true;
  }

  // Resume a verified continuing sensor epoch. Unlike restoreCount(), the
  // supplied raw anchor belongs to this total, so the next sample adds its
  // actual delta instead of discarding steps taken during processor reboot.
  void restoreCountAndRaw(uint32_t count, uint16_t raw) {
    if (!has_day_) return;
    count_ = count;
    last_raw_ = raw;
    needs_baseline_ = false;
  }

  void sample(uint16_t raw) {
    if (!has_day_) return;
    if (needs_baseline_) {
      last_raw_ = raw;
      needs_baseline_ = false;
      return;
    }
    const uint32_t delta = static_cast<uint16_t>(raw - last_raw_);
    const uint32_t maximum = std::numeric_limits<uint32_t>::max();
    count_ = count_ > maximum - delta ? maximum : count_ + delta;
    last_raw_ = raw;
  }

  uint32_t count() const { return count_; }
  uint16_t lastRaw() const { return last_raw_; } // Valid after a baseline.
  int32_t dayKey() const { return day_key_; } // Valid only when hasDay().
  bool needsBaseline() const { return needs_baseline_; }
  bool hasDay() const { return has_day_; }

 private:
  int32_t day_key_ = 0;
  uint32_t count_ = 0;
  uint16_t last_raw_ = 0;
  bool has_day_ = false;
  bool needs_baseline_ = true;
};

}  // namespace watch
