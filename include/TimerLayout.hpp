#pragma once

#include <stdint.h>

namespace watch {

enum class TimerUiState { Ready, Running, Paused };
enum class TimerAction {
  None, SelectMinutes, SelectSeconds, Decrement, Increment, StartPause, Reset
};

struct TimerFaceData {
  uint32_t remaining_seconds;
  uint32_t duration_seconds;
  TimerUiState state;
  bool edit_seconds;
};

namespace timer_detail {

struct Rect {
  int x, y, w, h;
  bool contains(int px, int py) const {
    return px >= x && px < x + w && py >= y && py < y + h;
  }
};

// Visual controls and their touch regions share the same layout. The touch
// regions include a little padding so a finger need not hit the outline.
static constexpr Rect minutes_hit{20, 58, 96, 81};
static constexpr Rect seconds_hit{124, 58, 96, 81};
static constexpr Rect minus_button{53, 145, 54, 39};
static constexpr Rect plus_button{133, 145, 54, 39};
static constexpr Rect minus_hit{46, 140, 68, 47};
static constexpr Rect plus_hit{126, 140, 68, 47};
static constexpr Rect primary_button{67, 189, 106, 40};
static constexpr Rect primary_hit{61, 184, 118, 48};
static constexpr Rect pause_button{43, 164, 74, 43};
static constexpr Rect reset_button{123, 164, 74, 43};
static constexpr Rect pause_hit{38, 158, 82, 52};
static constexpr Rect reset_hit{120, 158, 82, 52};

}  // namespace timer_detail

inline TimerAction timerHitTest(TimerUiState state, int x, int y) {
  if (x < 0 || y < 0 || x >= 240 || y >= 240) return TimerAction::None;
  const int dx = 2 * x - 239;
  const int dy = 2 * y - 239;
  if (dx * dx + dy * dy > 239 * 239) return TimerAction::None;

  using namespace timer_detail;
  if (state == TimerUiState::Ready) {
    if (minutes_hit.contains(x, y)) return TimerAction::SelectMinutes;
    if (seconds_hit.contains(x, y)) return TimerAction::SelectSeconds;
    if (minus_hit.contains(x, y)) return TimerAction::Decrement;
    if (plus_hit.contains(x, y)) return TimerAction::Increment;
    if (primary_hit.contains(x, y)) return TimerAction::StartPause;
  } else {
    if (pause_hit.contains(x, y)) return TimerAction::StartPause;
    if (reset_hit.contains(x, y)) return TimerAction::Reset;
  }
  return TimerAction::None;
}

}  // namespace watch
