#pragma once

#include "WatchTiming.hpp"

#include <cstdint>

namespace watch {

// Schedule sensor snapshots separately from dirty-only flash writes. Start a
// full interval at boot, and after every checkpoint, including a failed write
// or unchanged totals. Delayed service never causes a burst of catch-up writes.
class StepCheckpoint {
 public:
  static constexpr uint32_t kIntervalMs = 30U * 60U * 1000U;

  void begin(uint32_t now_ms) { last_attempt_ms_ = now_ms; }

  uint32_t waitMs(uint32_t now_ms) const {
    return remainingMs(now_ms, last_attempt_ms_, kIntervalMs);
  }

  bool due(uint32_t now_ms) const { return waitMs(now_ms) == 0; }

  void attempted(uint32_t now_ms) { last_attempt_ms_ = now_ms; }

 private:
  uint32_t last_attempt_ms_ = 0;
};

}  // namespace watch
