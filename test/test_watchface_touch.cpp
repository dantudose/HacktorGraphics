#include "WatchfaceTouch.hpp"

#include <cassert>
#include <cstdio>

using watch::SwipeDirection;
using watch::TimerAction;
using watch::TimerUiState;
using watch::WatchfaceTouch;

namespace {

watch::WatchfaceTouchResult sample(
    WatchfaceTouch& touch, bool timer, TimerUiState state, bool pressed,
    int x, int y, uint32_t now, TimerAction action = TimerAction::None,
    SwipeDirection swipe = SwipeDirection::None) {
  const auto result = touch.update(timer, state, pressed, x, y, now);
  assert(result.timer_action == action);
  assert(result.swipe == swipe);
  assert(!result.open_settings);
  return result;
}

void timerTapActsOnlyOnRelease() {
  WatchfaceTouch touch;
  sample(touch, true, TimerUiState::Ready, false, 0, 0, 0);
  sample(touch, true, TimerUiState::Ready, true, 120, 209, 100);
  sample(touch, true, TimerUiState::Ready, true, 120, 209, 200);
  sample(touch, true, TimerUiState::Ready, false, 0, 0, 220,
         TimerAction::StartPause);
  sample(touch, true, TimerUiState::Running, false, 0, 0, 240);
}

void swipingAcrossStartDoesNotStart() {
  WatchfaceTouch touch;
  sample(touch, true, TimerUiState::Ready, false, 0, 0, 0);
  sample(touch, true, TimerUiState::Ready, true, 170, 209, 100);
  sample(touch, true, TimerUiState::Ready, true, 120, 209, 180);
  sample(touch, true, TimerUiState::Ready, true, 70, 209, 260);
  sample(touch, true, TimerUiState::Ready, false, 0, 0, 300,
         TimerAction::None, SwipeDirection::Left);
}

void swipingAcrossResetAndPauseDoesNeither() {
  WatchfaceTouch touch;
  sample(touch, true, TimerUiState::Running, false, 0, 0, 0);
  sample(touch, true, TimerUiState::Running, true, 160, 185, 100);
  sample(touch, true, TimerUiState::Running, true, 80, 185, 200);
  sample(touch, true, TimerUiState::Running, false, 0, 0, 250,
         TimerAction::None, SwipeDirection::Left);
}

void swipingAcrossAdjustmentsDoesNotEditDuration() {
  WatchfaceTouch touch;
  sample(touch, true, TimerUiState::Ready, false, 0, 0, 0);
  sample(touch, true, TimerUiState::Ready, true, 80, 165, 100);
  sample(touch, true, TimerUiState::Ready, true, 160, 165, 200);
  sample(touch, true, TimerUiState::Ready, false, 0, 0, 240,
         TimerAction::None, SwipeDirection::Right);
}

void adjustmentHoldConsumesContactBeforeALaterDrag() {
  WatchfaceTouch touch;
  sample(touch, true, TimerUiState::Ready, false, 0, 0, 0);
  sample(touch, true, TimerUiState::Ready, true, 160, 165, 100);
  sample(touch, true, TimerUiState::Ready, true, 160, 165, 600,
         TimerAction::Increment);
  sample(touch, true, TimerUiState::Ready, true, 160, 165, 720,
         TimerAction::Increment);
  sample(touch, true, TimerUiState::Ready, true, 80, 165, 740);
  sample(touch, true, TimerUiState::Ready, false, 0, 0, 780);
  sample(touch, true, TimerUiState::Ready, true, 160, 165, 800);
  sample(touch, true, TimerUiState::Ready, false, 0, 0, 850,
         TimerAction::Increment);
}

void smallDragWithinButtonDoesNotTapOrSwipe() {
  WatchfaceTouch touch;
  sample(touch, true, TimerUiState::Ready, false, 0, 0, 0);
  sample(touch, true, TimerUiState::Ready, true, 160, 165, 100);
  sample(touch, true, TimerUiState::Ready, true, 180, 165, 200);
  sample(touch, true, TimerUiState::Ready, true, 160, 165, 250);
  sample(touch, true, TimerUiState::Ready, false, 0, 0, 300);
}

void otherFacesNeverOperateTimerControls() {
  WatchfaceTouch touch;
  sample(touch, false, TimerUiState::Ready, false, 0, 0, 0);
  sample(touch, false, TimerUiState::Ready, true, 120, 209, 100);
  sample(touch, false, TimerUiState::Ready, false, 0, 0, 200);
  sample(touch, false, TimerUiState::Ready, true, 160, 165, 300);
  sample(touch, false, TimerUiState::Ready, true, 160, 165, 1000);
  sample(touch, false, TimerUiState::Ready, false, 0, 0, 1100);
}

void consecutiveSwipesNeedNoExtraIdleSample() {
  WatchfaceTouch touch;
  sample(touch, false, TimerUiState::Ready, false, 0, 0, 0);
  sample(touch, false, TimerUiState::Ready, true, 190, 120, 100);
  sample(touch, false, TimerUiState::Ready, true, 70, 120, 200);
  sample(touch, false, TimerUiState::Ready, false, 0, 0, 240,
         TimerAction::None, SwipeDirection::Left);
  sample(touch, true, TimerUiState::Ready, true, 70, 120, 260);
  sample(touch, true, TimerUiState::Ready, true, 190, 120, 360);
  sample(touch, true, TimerUiState::Ready, false, 0, 0, 400,
         TimerAction::None, SwipeDirection::Right);
  sample(touch, false, TimerUiState::Ready, false, 0, 0, 420);
}

void swipeOntoTimerAllowsNextTapImmediately() {
  WatchfaceTouch touch;
  sample(touch, false, TimerUiState::Ready, false, 0, 0, 0);
  sample(touch, false, TimerUiState::Ready, true, 190, 120, 100);
  sample(touch, false, TimerUiState::Ready, true, 70, 120, 200);
  sample(touch, false, TimerUiState::Ready, false, 0, 0, 240,
         TimerAction::None, SwipeDirection::Left);
  sample(touch, true, TimerUiState::Ready, true, 120, 209, 260);
  sample(touch, true, TimerUiState::Ready, false, 0, 0, 320,
         TimerAction::StartPause);
}

void resetRequiresLiftBeforeEitherKindOfInput() {
  WatchfaceTouch touch;
  sample(touch, true, TimerUiState::Ready, false, 0, 0, 0);
  sample(touch, true, TimerUiState::Ready, true, 160, 165, 100);
  touch.reset(); // Screen wake/change, expiry, or I2C failure.
  sample(touch, true, TimerUiState::Ready, true, 80, 165, 200);
  sample(touch, true, TimerUiState::Ready, true, 80, 165, 700);
  sample(touch, true, TimerUiState::Ready, false, 0, 0, 740);
  sample(touch, true, TimerUiState::Ready, true, 120, 209, 800);
  sample(touch, true, TimerUiState::Ready, false, 0, 0, 900,
         TimerAction::StartPause);
}

void liveOffsetsPrecedeTheReleaseCommit() {
  WatchfaceTouch touch;
  sample(touch, false, TimerUiState::Ready, false, 0, 0, 0);
  auto result = sample(touch, false, TimerUiState::Ready, true, 180, 120, 100);
  assert(!result.dragging && !result.drag_released && result.drag_offset == 0);
  result = sample(touch, false, TimerUiState::Ready, true, 165, 120, 120);
  assert(result.dragging && !result.drag_released && result.drag_offset == -15);
  result = sample(touch, false, TimerUiState::Ready, true, 80, 120, 200);
  assert(result.dragging && !result.drag_released && result.drag_offset == -100);
  result = sample(touch, false, TimerUiState::Ready, false, 0, 0, 250,
                  TimerAction::None, SwipeDirection::Left);
  assert(!result.dragging && result.drag_released && result.drag_offset == -100);
  result = sample(touch, false, TimerUiState::Ready, false, 0, 0, 270);
  assert(!result.dragging && !result.drag_released && result.drag_offset == 0);
}

void subthresholdTimerDragReportsSnapBackWithoutTap() {
  WatchfaceTouch touch;
  sample(touch, true, TimerUiState::Ready, false, 0, 0, 0);
  sample(touch, true, TimerUiState::Ready, true, 120, 209, 100);
  auto result = sample(touch, true, TimerUiState::Ready, true, 140, 209, 150);
  assert(result.dragging && result.drag_offset == 20);
  result = sample(touch, true, TimerUiState::Ready, false, 0, 0, 200);
  assert(result.drag_released && result.drag_offset == 20);
  // The confirmed lift rearms controls immediately for the next deliberate tap.
  sample(touch, true, TimerUiState::Ready, true, 120, 209, 220);
  sample(touch, true, TimerUiState::Ready, false, 0, 0, 280,
         TimerAction::StartPause);
}

void aTimerDragCanReverseThroughTheOrigin() {
  WatchfaceTouch touch;
  sample(touch, true, TimerUiState::Ready, false, 0, 0, 0);
  sample(touch, true, TimerUiState::Ready, true, 120, 209, 100);
  auto result = sample(touch, true, TimerUiState::Ready, true, 70, 209, 180);
  assert(result.dragging && result.drag_offset == -50);
  result = sample(touch, true, TimerUiState::Ready, true, 120, 209, 250);
  assert(result.dragging && result.drag_offset == 0);
  result = sample(touch, true, TimerUiState::Ready, true, 170, 209, 300);
  assert(result.dragging && result.drag_offset == 50);
  result = sample(touch, true, TimerUiState::Ready, false, 0, 0, 350,
                  TimerAction::None, SwipeDirection::Right);
  assert(result.drag_released && result.drag_offset == 50);
}

void slowHeldDragCannotTurnBackIntoAnAdjustmentHold() {
  WatchfaceTouch touch;
  sample(touch, true, TimerUiState::Ready, false, 0, 0, 0);
  sample(touch, true, TimerUiState::Ready, true, 160, 165, 100);
  auto result = sample(touch, true, TimerUiState::Ready, true, 140, 165, 200);
  assert(result.dragging && result.drag_offset == -20);
  result = sample(touch, true, TimerUiState::Ready, true, 160, 165, 2000);
  assert(result.dragging && result.drag_offset == 0);
  result = sample(touch, true, TimerUiState::Ready, true, 160, 165, 3000);
  assert(result.dragging && result.drag_offset == 0);
  result = sample(touch, true, TimerUiState::Ready, false, 0, 0, 3100);
  assert(result.drag_released && result.drag_offset == 0);
}

void anAdjustmentHoldPreventsLiveDragAsWellAsNavigation() {
  WatchfaceTouch touch;
  sample(touch, true, TimerUiState::Ready, false, 0, 0, 0);
  sample(touch, true, TimerUiState::Ready, true, 160, 165, 100);
  auto result = sample(touch, true, TimerUiState::Ready, true, 160, 165, 600,
                       TimerAction::Increment);
  assert(!result.dragging && !result.drag_released);
  result = sample(touch, true, TimerUiState::Ready, true, 80, 165, 650);
  assert(!result.dragging && !result.drag_released && result.drag_offset == 0);
  result = sample(touch, true, TimerUiState::Ready, false, 0, 0, 700);
  assert(!result.dragging && !result.drag_released);
}

void resettingALiveDragRequiresLiftBeforeAnyNewInput() {
  WatchfaceTouch touch;
  sample(touch, true, TimerUiState::Ready, false, 0, 0, 0);
  sample(touch, true, TimerUiState::Ready, true, 170, 209, 100);
  auto result = sample(touch, true, TimerUiState::Ready, true, 100, 209, 200);
  assert(result.dragging);
  touch.reset();
  result = sample(touch, true, TimerUiState::Ready, true, 70, 209, 250);
  assert(!result.dragging && !result.drag_released && result.drag_offset == 0);
  result = sample(touch, true, TimerUiState::Ready, false, 0, 0, 300);
  assert(!result.drag_released);
  sample(touch, true, TimerUiState::Ready, true, 120, 209, 350);
  sample(touch, true, TimerUiState::Ready, false, 0, 0, 400,
         TimerAction::StartPause);
}

}  // namespace

int main() {
  timerTapActsOnlyOnRelease();
  swipingAcrossStartDoesNotStart();
  swipingAcrossResetAndPauseDoesNeither();
  swipingAcrossAdjustmentsDoesNotEditDuration();
  adjustmentHoldConsumesContactBeforeALaterDrag();
  smallDragWithinButtonDoesNotTapOrSwipe();
  otherFacesNeverOperateTimerControls();
  consecutiveSwipesNeedNoExtraIdleSample();
  swipeOntoTimerAllowsNextTapImmediately();
  resetRequiresLiftBeforeEitherKindOfInput();
  liveOffsetsPrecedeTheReleaseCommit();
  subthresholdTimerDragReportsSnapBackWithoutTap();
  aTimerDragCanReverseThroughTheOrigin();
  slowHeldDragCannotTurnBackIntoAnAdjustmentHold();
  anAdjustmentHoldPreventsLiveDragAsWellAsNavigation();
  resettingALiveDragRequiresLiftBeforeAnyNewInput();
  std::puts("Watchface touch: all 16 scenarios passed.");
}
