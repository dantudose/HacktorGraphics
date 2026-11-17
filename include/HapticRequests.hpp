#pragma once

#include <cstdint>

namespace watch {

enum class HapticPlayback { None, Timer, Confirmation };

// Bounded pending requests only: main owns playback and any preemption. Timer
// alerts take priority; a settings confirmation is useful only while its screen
// is visible and the manual action is still recent. Nothing queues at boot.
class HapticRequests {
 public:
  void requestTimer() {
    timer_pending_ = true;
    cancelConfirmation();
  }

  void requestConfirmation(uint32_t now_ms) {
    if (timer_pending_) return;
    confirmation_pending_ = true;
    confirmation_since_ms_ = now_ms;
  }

  void mute() {
    timer_pending_ = false;
    cancelConfirmation();
  }

  void cancelConfirmation() {
    confirmation_pending_ = false;
    confirmation_since_ms_ = 0;
  }

  void expireConfirmation(uint32_t now_ms, bool screen_visible) {
    if (confirmation_pending_ &&
        (!screen_visible || now_ms - confirmation_since_ms_ >= 1000U)) {
      cancelConfirmation();
    }
  }

  bool timerPending() const { return timer_pending_; }
  bool pending() const { return timer_pending_ || confirmation_pending_; }

  HapticPlayback take(uint32_t now_ms, bool screen_visible) {
    expireConfirmation(now_ms, screen_visible);
    if (timer_pending_) {
      timer_pending_ = false;
      return HapticPlayback::Timer;
    }
    if (confirmation_pending_) {
      cancelConfirmation();
      return HapticPlayback::Confirmation;
    }
    return HapticPlayback::None;
  }

 private:
  bool timer_pending_ = false;
  bool confirmation_pending_ = false;
  uint32_t confirmation_since_ms_ = 0;
};

}  // namespace watch
