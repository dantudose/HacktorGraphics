#pragma once

#include <cstdint>

namespace watch {

struct ButtonEvents {
  bool pressed = false;
  bool released = false;
  bool short_press = false;
  bool long_press = false;
};

// The caller converts the GPIO level to true when pressed. Poll while
// shouldStayAwake() is true so button debounce/gestures cannot be lost in sleep.
// Unsigned subtraction keeps all timing valid across millis() rollover.
class WatchButton {
 public:
  explicit WatchButton(uint32_t debounce_ms = 30, uint32_t long_press_ms = 800)
      : debounce_ms_(debounce_ms), long_press_ms_(long_press_ms) {}

  // A button already held at startup must first be released before it can
  // produce gestures. Resetting a held button also safely cancels that gesture.
  void reset(bool pressed, uint32_t now) {
    raw_pressed_ = pressed;
    pressed_ = pressed;
    raw_since_ = now;
    press_since_ = now;
    ignore_until_release_ = pressed;
    press_candidate_ = false;
    gesture_active_ = false;
    long_reported_ = false;
  }

  ButtonEvents update(bool raw_pressed, uint32_t now) {
    ButtonEvents events;

    // Finish the previous raw level before recording a new edge. This also
    // handles a release observed after a blocking display initialization.
    settle(now, events);
    if (raw_pressed != raw_pressed_) {
      raw_pressed_ = raw_pressed;
      raw_since_ = now;
      if (raw_pressed && !pressed_ && !ignore_until_release_ &&
          !press_candidate_) {
        press_since_ = now;
        press_candidate_ = true;
      }
    }
    settle(now, events);

    if (gesture_active_ && !long_reported_) {
      // A pending release stops hold timing at its edge, rather than adding
      // the release debounce interval to the user's actual hold duration.
      const uint32_t held_until = raw_pressed_ ? now : raw_since_;
      if (held_until - press_since_ >= long_press_ms_) {
        events.long_press = true;
        long_reported_ = true;
      }
    }
    return events;
  }

  bool isPressed() const { return pressed_; }

  // A rejected press bounce is still pending until the release settles.
  bool isSettled() const {
    return raw_pressed_ == pressed_ && !press_candidate_;
  }

  bool shouldStayAwake() const {
    return pressed_ || raw_pressed_ || !isSettled();
  }

 private:
  void settle(uint32_t now, ButtonEvents& events) {
    if (now - raw_since_ < debounce_ms_) return;

    if (raw_pressed_ == pressed_) {
      if (!pressed_) press_candidate_ = false;
      return;
    }

    pressed_ = raw_pressed_;
    if (pressed_) {
      if (ignore_until_release_) return;
      events.pressed = true;
      gesture_active_ = true;
      press_candidate_ = false;
      long_reported_ = false;
      return;
    }

    events.released = true;
    if (ignore_until_release_) {
      ignore_until_release_ = false;
    } else if (gesture_active_ && !long_reported_) {
      if (raw_since_ - press_since_ >= long_press_ms_) {
        events.long_press = true;
      } else {
        events.short_press = true;
      }
    }
    gesture_active_ = false;
    press_candidate_ = false;
    long_reported_ = false;
  }

  const uint32_t debounce_ms_;
  const uint32_t long_press_ms_;
  bool raw_pressed_ = false;
  bool pressed_ = false;
  bool ignore_until_release_ = false;
  bool press_candidate_ = false;
  bool gesture_active_ = false;
  bool long_reported_ = false;
  uint32_t raw_since_ = 0;
  uint32_t press_since_ = 0;
};

}  // namespace watch
