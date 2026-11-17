#pragma once

#include "TimerTouch.hpp"
#include "WatchSwipe.hpp"

namespace watch {

struct WatchfaceTouchResult {
  SwipeDirection swipe = SwipeDirection::None;
  TimerAction timer_action = TimerAction::None;
  bool dragging = false;
  int drag_offset = 0;
  bool drag_released = false;
  bool open_settings = false;
  bool settings_dragging = false;
  int settings_offset = 0;
  bool settings_released = false;
};

// Share each contact between carousel dragging, the top pull-down and timer
// controls. The first clear intent owns the contact until the finger lifts.
class WatchfaceTouch {
 public:
  void reset() {
    swipe_.reset();
    timer_.reset();
    armed_ = false;
    clearPullDown();
  }

  WatchfaceTouchResult update(bool timer_visible, TimerUiState state,
                             bool pressed, int x, int y, uint32_t now_ms) {
    WatchfaceTouchResult result;
    if (pressed && (x < 0 || x >= 240 || y < 0 || y >= 240)) {
      reset();
      return result;
    }
    if (pressed && armed_) {
      if (!down_) {
        down_ = true;
        top_candidate_ = y <= 50;
        start_x_ = x;
        start_y_ = y;
      }
      last_x_ = x;
      last_y_ = y;
    }
    if (pull_down_) {
      const int dy = last_y_ - start_y_;
      result.settings_dragging = pressed;
      result.settings_offset = dy > 0 ? dy : 0;
      if (!pressed) {
        result.settings_released = true;
        result.open_settings = dy >= 45 && 2 * magnitude(last_x_ - start_x_) <= dy;
        rearmAfterLift(state, now_ms);
      }
      // A claimed vertical contact cannot turn into a timer tap or carousel
      // drag, even if the finger reverses before release and cancels opening.
      return result;
    }

    const bool was_dragging = swipe_.dragging();
    const int previous_offset = swipe_.offset();
    result.swipe = swipe_.update(pressed, x, y, now_ms);
    result.dragging = swipe_.dragging();
    result.drag_released = was_dragging && !pressed;
    result.drag_offset = result.drag_released ? previous_offset : swipe_.offset();
    if (result.dragging) {
      top_candidate_ = false;
      timer_.reset();
      return result;
    }
    if (result.drag_released) {
      // This transition already includes a confirmed lift. Allow the next
      // contact immediately, without needing a second idle release sample.
      rearmAfterLift(state, now_ms);
      return result;
    }
    if (pressed && armed_ && top_candidate_) {
      const int dx = magnitude(x - start_x_);
      const int dy = y - start_y_;
      if (dy >= 12 && dy >= 2 * dx) {
        pull_down_ = true;
        swipe_.reset();
        timer_.reset();
        result.settings_dragging = true;
        result.settings_offset = dy;
        return result;
      }
      // An upward gesture from the top is consumed by neither menu opening
      // nor a later downward reversal in the same contact.
      if (dy <= -12 && -dy >= 2 * dx) top_candidate_ = false;
    }
    if (timer_visible) {
      result.timer_action = timer_.update(state, pressed, x, y, now_ms);
      if (pressed && result.timer_action != TimerAction::None) {
        swipe_.reset();
        top_candidate_ = false;
      }
    } else {
      timer_.reset();
    }
    if (!pressed) {
      clearPullDown();
      armed_ = true;
    }
    return result;
  }

 private:
  static int magnitude(int value) { return value < 0 ? -value : value; }

  void clearPullDown() {
    down_ = false;
    top_candidate_ = false;
    pull_down_ = false;
  }

  void rearmAfterLift(TimerUiState state, uint32_t now_ms) {
    reset();
    armed_ = true;
    swipe_.update(false, 0, 0, now_ms);
    timer_.update(state, false, 0, 0, now_ms);
  }

  WatchSwipe swipe_;
  TimerTouch timer_;
  bool armed_ = false;
  bool down_ = false;
  bool top_candidate_ = false;
  bool pull_down_ = false;
  int start_x_ = 0;
  int start_y_ = 0;
  int last_x_ = 0;
  int last_y_ = 0;
};

}  // namespace watch
