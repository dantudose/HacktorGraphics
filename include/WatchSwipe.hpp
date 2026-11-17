#pragma once

#include <cstdint>

namespace watch {

enum class SwipeDirection { None, Left, Right };

// Track horizontal dragging, then decide navigation when the finger lifts.
// Coordinates already follow the display rotation; release coordinates are unused.
class WatchSwipe {
 public:
  // A finger already resting on a newly visible screen cannot navigate it.
  void reset() {
    armed_ = false;
    down_ = false;
    dragging_ = false;
    rejected_ = false;
    offset_ = 0;
  }

  bool active() const { return down_; }
  bool dragging() const { return dragging_; }
  int offset() const { return offset_; }

  SwipeDirection update(bool pressed, int x, int y, uint32_t /*now_ms*/) {
    if (!pressed) {
      SwipeDirection direction = SwipeDirection::None;
      if (dragging_ && magnitude(offset_) >= 45) {
        direction = offset_ < 0 ? SwipeDirection::Left : SwipeDirection::Right;
      }
      armed_ = true;
      down_ = false;
      dragging_ = false;
      rejected_ = false;
      offset_ = 0;
      return direction;
    }

    if (x < 0 || x >= 240 || y < 0 || y >= 240) {
      reset();
      return SwipeDirection::None;
    }
    if (!armed_) return SwipeDirection::None;

    if (!down_) {
      down_ = true;
      start_x_ = x;
      start_y_ = y;
      max_vertical_ = 0;
      dragging_ = false;
      rejected_ = false;
    }
    if (rejected_) return SwipeDirection::None;

    const int dx = x - start_x_;
    const int horizontal = magnitude(dx);
    const int vertical = magnitude(y - start_y_);
    if (vertical > max_vertical_) max_vertical_ = vertical;
    if (!dragging_) {
      // Determine intent before taking ownership from a timer tap/hold. A
      // vertical contact cannot become a carousel drag later in the contact.
      if (max_vertical_ > 35 || (vertical >= 12 && vertical >= horizontal)) {
        rejected_ = true;
        return SwipeDirection::None;
      }
      if (horizontal < 12 || horizontal < 2 * vertical) return SwipeDirection::None;
      dragging_ = true;
    }
    // Once horizontal intent is established, vertical drift does not break
    // the drag. Reversing through the origin can preview either neighbour.
    offset_ = dx;
    return SwipeDirection::None;
  }

 private:
  static int magnitude(int value) { return value < 0 ? -value : value; }

  bool armed_ = false;
  bool down_ = false;
  bool dragging_ = false;
  bool rejected_ = false;
  int start_x_ = 0;
  int start_y_ = 0;
  int offset_ = 0;
  int max_vertical_ = 0;
};

}  // namespace watch
