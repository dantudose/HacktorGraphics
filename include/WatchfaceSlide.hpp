#pragma once

#include <cstdint>

#include "WatchSwipe.hpp"

namespace watch {

struct SlideFrame {
  int outgoing_x;
  int incoming_x;
  bool finished;
};

// Follow a drag, then settle through the normal main loop. There are no waits,
// allocations, or rendering dependencies. Missed frames never extend settling.
class WatchfaceSlide {
 public:
  void start(SwipeDirection direction, uint32_t now_ms) {
    if (direction == SwipeDirection::None) {
      cancel();
      return;
    }
    direction_ = direction;
    started_ms_ = now_ms;
    mode_ = Mode::Settling;
    legacy_full_slide_ = true;
    duration_ms_ = 200;
    has_presented_ = false;
  }

  void drag(int signed_offset, uint32_t now_ms, int width = 240) {
    if (width <= 0) {
      cancel();
      return;
    }
    if (mode_ != Mode::Dragging) {
      mode_ = Mode::Dragging;
      started_ms_ = now_ms;
      has_presented_ = false;
    }
    offset_ = clamp(signed_offset, width);
    // Retain the adjacent face when a reversal passes exactly through zero.
    if (offset_ < 0) direction_ = SwipeDirection::Left;
    else if (offset_ > 0) direction_ = SwipeDirection::Right;
    else if (direction_ == SwipeDirection::None) direction_ = SwipeDirection::Left;
  }

  void release(bool commit, uint32_t now_ms, int width = 240) {
    if (mode_ != Mode::Dragging) return;
    if (width <= 0) {
      cancel();
      return;
    }
    start_offset_ = clamp(offset_, width);
    target_offset_ = commit && start_offset_ != 0
        ? (start_offset_ < 0 ? -width : width) : 0;
    const uint64_t distance = magnitude(target_offset_ - start_offset_);
    // Short remaining distances settle quickly without a long idle animation.
    const uint32_t scaled = static_cast<uint32_t>((distance * 200U + width - 1) / width);
    duration_ms_ = distance == 0 ? 0 : (scaled < 60 ? 60 : (scaled > 200 ? 200 : scaled));
    started_ms_ = now_ms;
    mode_ = Mode::Settling;
    legacy_full_slide_ = false;
    has_presented_ = false;
  }

  void cancel() {
    mode_ = Mode::Idle;
    direction_ = SwipeDirection::None;
    has_presented_ = false;
  }

  bool active() const { return mode_ != Mode::Idle; }
  bool dragging() const { return mode_ == Mode::Dragging; }

  bool due(uint32_t now_ms) const {
    if (!active()) return false;
    if (dragging()) {
      const bool changed = !has_presented_ || offset_ != presented_offset_ ||
                           direction_ != presented_direction_;
      return changed && (!has_presented_ || now_ms - presented_ms_ >= 16U);
    }
    return !has_presented_ || now_ms - started_ms_ >= duration_ms_ ||
           now_ms - presented_ms_ >= 16U;
  }

  SlideFrame frame(uint32_t now_ms, int width = 240) const {
    if (!active() || width <= 0) return SlideFrame{0, 0, true};
    int outgoing = clamp(offset_, width);
    bool finished = false;
    if (!dragging()) {
      const uint32_t elapsed = now_ms - started_ms_;
      finished = elapsed >= duration_ms_;
      const int start = legacy_full_slide_ ? 0 : start_offset_;
      const int target = legacy_full_slide_
          ? (direction_ == SwipeDirection::Left ? -width : width) : target_offset_;
      outgoing = target;
      if (!finished) {
        const uint64_t remaining = duration_ms_ - elapsed;
        const uint64_t cube = uint64_t(duration_ms_) * duration_ms_ * duration_ms_;
        const int delta = target - start;
        const uint64_t rest = (magnitude(delta) * remaining * remaining * remaining +
                               cube / 2) / cube;
        outgoing -= delta < 0 ? -static_cast<int>(rest) : static_cast<int>(rest);
      }
    }
    return SlideFrame{outgoing,
                      outgoing + (direction_ == SwipeDirection::Left ? width : -width),
                      finished};
  }

  void presented(uint32_t now_ms) {
    if (!active()) return;
    presented_ms_ = now_ms;
    presented_offset_ = offset_;
    presented_direction_ = direction_;
    has_presented_ = true;
  }

 private:
  enum class Mode { Idle, Dragging, Settling };

  static int clamp(int value, int width) {
    return value < -width ? -width : (value > width ? width : value);
  }

  static uint64_t magnitude(int value) {
    return value < 0 ? static_cast<uint64_t>(-int64_t(value)) : uint64_t(value);
  }

  Mode mode_ = Mode::Idle;
  SwipeDirection direction_ = SwipeDirection::None;
  SwipeDirection presented_direction_ = SwipeDirection::None;
  uint32_t started_ms_ = 0;
  uint32_t presented_ms_ = 0;
  uint32_t duration_ms_ = 200;
  int offset_ = 0;
  int start_offset_ = 0;
  int target_offset_ = 0;
  int presented_offset_ = 0;
  bool legacy_full_slide_ = false;
  bool has_presented_ = false;
};

}  // namespace watch
