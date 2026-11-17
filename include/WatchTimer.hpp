#pragma once

#include <cstdint>
#include <limits>

namespace watch {

enum class TimerState { Ready, Running, Paused };

// Feed this timer esp_timer_get_time(), not the wall clock. The monotonic
// source includes light sleep and is unaffected by BLE time corrections.
class WatchTimer {
 public:
  static constexpr uint32_t MAX_SECONDS = 99U * 60U + 59U;

  explicit WatchTimer(uint32_t duration_seconds = 5U * 60U)
      : duration_seconds_(clampDuration(duration_seconds)),
        remaining_us_(static_cast<uint64_t>(duration_seconds_) * 1000000ULL) {}

  TimerState state() const { return state_; }
  uint32_t durationSeconds() const { return duration_seconds_; }

  // A positive fractional millisecond stays positive, so the display cannot
  // show zero before the countdown has expired.
  uint32_t remainingMs(uint64_t now_us) const {
    return static_cast<uint32_t>((remainingUs(now_us) + 999ULL) / 1000ULL);
  }

  // Editing is allowed only in Ready. Reset first to edit a paused timer.
  // Zero is a valid selection but cannot be started.
  bool setDurationSeconds(uint32_t seconds) {
    if (state_ != TimerState::Ready) return false;
    duration_seconds_ = clampDuration(seconds);
    remaining_us_ = static_cast<uint64_t>(duration_seconds_) * 1000000ULL;
    return true;
  }

  // Adjust the selected field without carrying into the other field.
  bool adjustMinutes(int delta) {
    if (state_ != TimerState::Ready) return false;
    const int64_t minutes = clampField(
        static_cast<int64_t>(duration_seconds_ / 60U) + delta, 99);
    return setDurationSeconds(static_cast<uint32_t>(minutes) * 60U +
                              duration_seconds_ % 60U);
  }

  bool adjustSeconds(int delta) {
    if (state_ != TimerState::Ready) return false;
    const int64_t seconds = clampField(
        static_cast<int64_t>(duration_seconds_ % 60U) + delta, 59);
    return setDurationSeconds((duration_seconds_ / 60U) * 60U +
                              static_cast<uint32_t>(seconds));
  }

  bool start(uint64_t now_us) {
    if ((state_ != TimerState::Ready && state_ != TimerState::Paused) ||
        remaining_us_ == 0) {
      return false;
    }
    started_us_ = now_us;
    state_ = TimerState::Running;
    return true;
  }

  bool pause(uint64_t now_us) {
    if (state_ != TimerState::Running) return false;
    const uint64_t remaining_us = remainingUs(now_us);
    // Keep the completion event pending for update(), its single consumer.
    if (remaining_us == 0) return false;
    remaining_us_ = remaining_us;
    state_ = TimerState::Paused;
    return true;
  }

  void reset() {
    state_ = TimerState::Ready;
    remaining_us_ = static_cast<uint64_t>(duration_seconds_) * 1000000ULL;
    started_us_ = 0;
  }

  // Check before processing controls. True is emitted once per completion,
  // including after a sleep or blocking call that passed the deadline. Restore
  // the configured duration immediately, ready for another explicit Start;
  // completion never starts a repeating countdown.
  bool update(uint64_t now_us) {
    if (state_ != TimerState::Running || remainingUs(now_us) != 0) return false;
    reset();
    return true;
  }

  // No periodic awake polling is needed: cap light sleep by this deadline.
  uint64_t waitUs(uint64_t now_us) const {
    return state_ == TimerState::Running
        ? remainingUs(now_us) : std::numeric_limits<uint64_t>::max();
  }

 private:
  static uint32_t clampDuration(uint32_t seconds) {
    return seconds > MAX_SECONDS ? MAX_SECONDS : seconds;
  }

  static int64_t clampField(int64_t value, int64_t maximum) {
    return value < 0 ? 0 : (value > maximum ? maximum : value);
  }

  uint64_t remainingUs(uint64_t now_us) const {
    if (state_ != TimerState::Running) return remaining_us_;
    const uint64_t elapsed_us = now_us - started_us_;
    return elapsed_us >= remaining_us_ ? 0 : remaining_us_ - elapsed_us;
  }

  TimerState state_ = TimerState::Ready;
  uint32_t duration_seconds_;
  uint64_t remaining_us_;
  uint64_t started_us_ = 0;
};

}  // namespace watch
