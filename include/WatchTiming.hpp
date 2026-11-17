#pragma once

#include <algorithm>
#include <cstdint>

namespace watch {

// Feed this clock esp_timer_get_time(), which includes RTC-measured light sleep.
// Anchoring instead of accumulating loop deltas preserves sub-millisecond time
// and allows a CTS update to replace the clock without counting a scan twice.
class Clock {
 public:
  void set(uint64_t time_ms, uint64_t now_us) {
    base_ms_ = time_ms;
    anchor_us_ = now_us;
  }

  uint64_t at(uint64_t now_us) const {
    return base_ms_ + (now_us - anchor_us_) / 1000ULL;
  }

 private:
  uint64_t base_ms_ = 0;
  uint64_t anchor_us_ = 0;
};

// Unsigned subtraction also handles the 32-bit millis() rollover. Callers
// check at least once per interval (all intervals here are under one hour).
inline uint32_t remainingMs(uint32_t now, uint32_t since, uint32_t interval) {
  const uint32_t elapsed = now - since;
  return elapsed >= interval ? 0 : interval - elapsed;
}

inline uint32_t ctsWaitMs(uint32_t now, bool synced, uint32_t last_sync,
                          uint32_t last_attempt, uint32_t sync_interval,
                          uint32_t retry_interval) {
  // Both the normal sync deadline and the minimum attempt spacing must pass.
  return std::max(synced ? remainingMs(now, last_sync, sync_interval) : 0U,
                  remainingMs(now, last_attempt, retry_interval));
}

}  // namespace watch
