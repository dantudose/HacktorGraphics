#pragma once

#include <cstdint>
#include <limits>

namespace watch {

// The DRV2605 plays finite timer and confirmation effects. This policy schedules
// bounded status polls, an optional normal stop, and the quiet interval after it.
// Main owns motor commands, the shared supply and any display wake.
class HapticAlert {
 public:
  static constexpr uint32_t POLL_MS = 25;
  static constexpr uint32_t MAX_PLAY_MS = 3000;
  static constexpr uint32_t SETTLE_MS = 200;

  bool start(uint32_t now_ms, uint32_t stop_after_ms = 0) {
    if (active()) return false;
    stage_ = Stage::Playing;
    stage_since_ms_ = now_ms;
    last_poll_ms_ = now_ms;
    stop_after_ms_ = stop_after_ms > MAX_PLAY_MS ? MAX_PLAY_MS : stop_after_ms;
    return true;
  }

  bool active() const { return stage_ != Stage::Idle; }
  bool playing() const { return stage_ == Stage::Playing; }
  bool settling() const { return stage_ == Stage::Settling; }

  bool pollDue(uint32_t now_ms) const {
    return playing() && !timedOut(now_ms) &&
        now_ms - last_poll_ms_ >= POLL_MS;
  }

  void markPolled(uint32_t now_ms) {
    if (playing()) last_poll_ms_ = now_ms;
  }

  bool timedOut(uint32_t now_ms) const {
    return playing() && now_ms - stage_since_ms_ >= MAX_PLAY_MS;
  }

  // A normal duration does not shorten the safety watchdog. The first stop is
  // due exactly at its deadline, even after a recent status read. Once main
  // marks that attempt, failed stop commands retry at the usual poll interval.
  bool stopDue(uint32_t now_ms) const {
    return playing() && !timedOut(now_ms) && stop_after_ms_ &&
        now_ms - stage_since_ms_ >= stop_after_ms_ &&
        (pollDue(now_ms) || last_poll_ms_ - stage_since_ms_ < stop_after_ms_);
  }

  // Call after commanding the motor to stop, including on timeout or error.
  // Repeated calls cannot postpone cleanup or extend the supply-on interval.
  bool settle(uint32_t now_ms) {
    if (!playing()) return false;
    stage_ = Stage::Settling;
    stage_since_ms_ = now_ms;
    return true;
  }

  // Emits completion once, after mechanical settling. A late poll advances
  // directly to Idle; it never replays hardware commands or starts an alert.
  bool update(uint32_t now_ms) {
    if (!settling() || now_ms - stage_since_ms_ < SETTLE_MS) return false;
    stage_ = Stage::Idle;
    return true;
  }

  // Zero means service a due poll/timeout/cleanup now. UINT32_MAX means there
  // is no alert deadline. Unsigned elapsed time handles millis() rollover.
  uint32_t waitMs(uint32_t now_ms) const {
    if (playing()) {
      const uint32_t poll_wait = remaining(now_ms, last_poll_ms_, POLL_MS);
      const uint32_t timeout_wait = remaining(now_ms, stage_since_ms_, MAX_PLAY_MS);
      uint32_t wait = poll_wait < timeout_wait ? poll_wait : timeout_wait;
      if (stop_after_ms_ && last_poll_ms_ - stage_since_ms_ < stop_after_ms_) {
        const uint32_t stop_wait = remaining(now_ms, stage_since_ms_, stop_after_ms_);
        if (stop_wait < wait) wait = stop_wait;
      }
      return wait;
    }
    return settling() ? remaining(now_ms, stage_since_ms_, SETTLE_MS)
                      : std::numeric_limits<uint32_t>::max();
  }

 private:
  enum class Stage { Idle, Playing, Settling };

  static uint32_t remaining(uint32_t now, uint32_t since, uint32_t interval) {
    const uint32_t elapsed = now - since;
    return elapsed >= interval ? 0U : interval - elapsed;
  }

  Stage stage_ = Stage::Idle;
  uint32_t stage_since_ms_ = 0;
  uint32_t last_poll_ms_ = 0;
  uint32_t stop_after_ms_ = 0;
};

}  // namespace watch
