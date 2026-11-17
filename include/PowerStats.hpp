#pragma once

#include <cstdint>
#include <limits>

namespace watch {

enum class WakeReason { Boot, Button, Motion, Other };

struct PowerSnapshot {
  uint64_t uptime_us = 0;
  uint64_t screen_on_us = 0;
  uint64_t light_sleep_us = 0;
  uint32_t wake_count = 0;
  uint32_t button_wakes = 0;
  uint32_t motion_wakes = 0;
  uint32_t other_wakes = 0; // Includes the first screen illumination at boot.
};

// Event-based measurements since begin(), with no polling or hardware access.
// Feed the same monotonic microsecond clock used for watch time and light sleep.
// A normal illuminated screen counts; a reset-held LCD/backlight rail glow does
// not. The caller reports each completed successful light-sleep interval once.
class PowerStats {
 public:
  void begin(uint64_t now_us) {
    started_us_ = now_us;
    screen_since_us_ = now_us;
    screen_on_ = false;
    begun_ = true;
    totals_ = PowerSnapshot{};
  }

  void screen(bool on, uint64_t now_us, WakeReason reason = WakeReason::Other) {
    if (!begun_ || on == screen_on_) return;
    // A bad timestamp cannot turn one interval into almost 2^64 microseconds.
    if (now_us < screen_since_us_) now_us = screen_since_us_;
    if (screen_on_) {
      totals_.screen_on_us = add(totals_.screen_on_us, now_us - screen_since_us_);
    } else {
      increment(totals_.wake_count);
      if (reason == WakeReason::Button) increment(totals_.button_wakes);
      else if (reason == WakeReason::Motion) increment(totals_.motion_wakes);
      else increment(totals_.other_wakes);
    }
    screen_on_ = on;
    screen_since_us_ = now_us;
  }

  void slept(uint64_t duration_us) {
    if (begun_) totals_.light_sleep_us = add(totals_.light_sleep_us, duration_us);
  }

  PowerSnapshot snapshot(uint64_t now_us) const {
    if (!begun_) return PowerSnapshot{};
    PowerSnapshot result = totals_;
    result.uptime_us = now_us >= started_us_ ? now_us - started_us_ : 0;
    if (screen_on_ && now_us > screen_since_us_) {
      result.screen_on_us = add(result.screen_on_us, now_us - screen_since_us_);
    }
    // Keep percentages valid if a caller supplies a stale timestamp or a
    // sleep duration longer than the observed uptime.
    if (result.screen_on_us > result.uptime_us) result.screen_on_us = result.uptime_us;
    if (result.light_sleep_us > result.uptime_us) result.light_sleep_us = result.uptime_us;
    return result;
  }

 private:
  static uint64_t add(uint64_t a, uint64_t b) {
    const uint64_t maximum = std::numeric_limits<uint64_t>::max();
    return a > maximum - b ? maximum : a + b;
  }

  static void increment(uint32_t& value) {
    if (value != std::numeric_limits<uint32_t>::max()) ++value;
  }

  uint64_t started_us_ = 0;
  uint64_t screen_since_us_ = 0;
  bool screen_on_ = false;
  bool begun_ = false;
  PowerSnapshot totals_{};
};

}  // namespace watch
