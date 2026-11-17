#pragma once

#include <cstdint>

#include "TimerLayout.hpp"

namespace watch {

// Taps act on release so a contact can become a swipe without operating a
// control. Only the adjustment buttons act while held, after a repeat delay.
// Reset after wake, a screen transition or an invalid touch sample: an existing
// finger must lift before it can operate controls on the newly visible screen.
class TimerTouch {
 public:
  void reset() {
    armed_ = false;
    down_ = false;
    repeating_ = false;
    held_action_ = TimerAction::None;
  }

  TimerAction update(TimerUiState state, bool pressed, int x, int y,
                     uint32_t now_ms) {
    if (!pressed) {
      TimerAction action = TimerAction::None;
      if (armed_ && down_ && !repeating_ && state == state_at_press_) {
        action = held_action_;
      }
      armed_ = true;
      down_ = false;
      repeating_ = false;
      held_action_ = TimerAction::None;
      return action;
    }
    if (!armed_) return TimerAction::None;

    const TimerAction hit = timerHitTest(state, x, y);
    if (!down_) {
      down_ = true;
      state_at_press_ = state;
      held_action_ = hit;
      start_x_ = x;
      start_y_ = y;
      last_repeat_ms_ = now_ms;
      repeating_ = false;
      return TimerAction::None;
    }
    // Movement cancels even inside a large button. Returning to the original
    // position or dragging into another control cannot reactivate the contact.
    if (state != state_at_press_ || hit != held_action_ ||
        x < start_x_ - 10 || x > start_x_ + 10 ||
        y < start_y_ - 10 || y > start_y_ + 10) {
      held_action_ = TimerAction::None;
      return TimerAction::None;
    }
    if (held_action_ != TimerAction::Increment &&
        held_action_ != TimerAction::Decrement) return TimerAction::None;

    const uint32_t interval = repeating_ ? 120U : 500U;
    if (now_ms - last_repeat_ms_ < interval) return TimerAction::None;
    last_repeat_ms_ = now_ms;
    repeating_ = true;
    return held_action_;
  }

 private:
  bool armed_ = false;
  bool down_ = false;
  bool repeating_ = false;
  TimerUiState state_at_press_ = TimerUiState::Ready;
  TimerAction held_action_ = TimerAction::None;
  int start_x_ = 0;
  int start_y_ = 0;
  uint32_t last_repeat_ms_ = 0;
};

}  // namespace watch
