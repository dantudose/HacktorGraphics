#pragma once

#include <cstdint>

namespace watch {

struct SettingsSlideFrame {
  int settings_y;
  int watchface_y;
  bool finished;
};

// A full-height drawer follows the finger, then settles in the main loop.
// Rendering and completion acknowledgement belong to the caller. There are no
// allocations, timers, or waits here, and skipped frames never extend settling.
class SettingsSlide {
 public:
  void begin(bool was_open, uint32_t now_ms) {
    origin_open_ = was_open;
    target_open_ = was_open;
    visibility_ = was_open ? kHeight : 0;
    start_visibility_ = visibility_;
    target_visibility_ = visibility_;
    started_ms_ = now_ms;
    mode_ = Mode::Dragging;
    has_presented_ = false;
  }

  void drag(int signed_dy, uint32_t now_ms) {
    if (!dragging()) return;
    (void)now_ms;  // Input updates must not restart the presentation cadence.
    visibility_ = clamp(int64_t(origin_open_ ? kHeight : 0) + signed_dy);
  }

  void release(bool commit, uint32_t now_ms) {
    if (!dragging()) return;
    target_open_ = commit ? !origin_open_ : origin_open_;
    start_visibility_ = visibility_;
    target_visibility_ = target_open_ ? kHeight : 0;
    const uint32_t distance = static_cast<uint32_t>(
        magnitude(target_visibility_ - start_visibility_));
    const uint32_t scaled = (distance * 200U + kHeight - 1U) / kHeight;
    duration_ms_ = distance == 0 ? 0 : (scaled < 60 ? 60 : scaled);
    started_ms_ = now_ms;
    mode_ = Mode::Settling;
    has_presented_ = false;
  }

  // Origin/target metadata remains available until the next begin().
  void cancel() {
    mode_ = Mode::Idle;
    has_presented_ = false;
  }

  bool active() const { return mode_ != Mode::Idle; }
  bool dragging() const { return mode_ == Mode::Dragging; }
  bool originOpen() const { return origin_open_; }
  bool targetOpen() const { return target_open_; }

  bool due(uint32_t now_ms) const {
    if (!active()) return false;
    if (dragging()) {
      const bool changed = !has_presented_ || visibility_ != presented_visibility_;
      return changed && (!has_presented_ || now_ms - presented_ms_ >= 16U);
    }
    return !has_presented_ || now_ms - started_ms_ >= duration_ms_ ||
           now_ms - presented_ms_ >= 16U;
  }

  SettingsSlideFrame frame(uint32_t now_ms) const {
    int visibility = visibility_;
    bool finished = !active();
    if (mode_ == Mode::Settling) {
      const uint32_t elapsed = now_ms - started_ms_;
      finished = elapsed >= duration_ms_;
      visibility = target_visibility_;
      if (!finished) {
        const uint64_t remaining = duration_ms_ - elapsed;
        const uint64_t cube = uint64_t(duration_ms_) * duration_ms_ * duration_ms_;
        const int delta = target_visibility_ - start_visibility_;
        const uint64_t rest = (magnitude(delta) * remaining * remaining * remaining +
                               cube / 2U) / cube;
        visibility -= delta < 0 ? -static_cast<int>(rest) : static_cast<int>(rest);
      }
    }
    return SettingsSlideFrame{visibility - kHeight, visibility, finished};
  }

  void presented(uint32_t now_ms) {
    if (!active()) return;
    presented_ms_ = now_ms;
    presented_visibility_ = frame(now_ms).watchface_y;
    has_presented_ = true;
  }

 private:
  enum class Mode { Idle, Dragging, Settling };
  static const int kHeight = 240;

  static int clamp(int64_t value) {
    return value < 0 ? 0 : (value > kHeight ? kHeight : static_cast<int>(value));
  }

  static uint64_t magnitude(int value) {
    return value < 0 ? static_cast<uint64_t>(-int64_t(value)) : uint64_t(value);
  }

  Mode mode_ = Mode::Idle;
  uint32_t started_ms_ = 0;
  uint32_t presented_ms_ = 0;
  uint32_t duration_ms_ = 0;
  int visibility_ = 0;
  int start_visibility_ = 0;
  int target_visibility_ = 0;
  int presented_visibility_ = 0;
  bool origin_open_ = false;
  bool target_open_ = false;
  bool has_presented_ = false;
};

}  // namespace watch
