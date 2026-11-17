#include "TimerTouch.hpp"

#include <cassert>
#include <cstdio>
#include <initializer_list>
#include <limits>

using watch::TimerAction;
using watch::TimerTouch;
using watch::TimerUiState;

namespace {

void lift(TimerTouch& touch, uint32_t now = 0,
          TimerUiState state = TimerUiState::Ready) {
  assert(touch.update(state, false, 0, 0, now) == TimerAction::None);
}

void controlsMatchTheVisibleState() {
  using watch::timerHitTest;
  assert(timerHitTest(TimerUiState::Ready, 69, 98) == TimerAction::SelectMinutes);
  assert(timerHitTest(TimerUiState::Ready, 171, 98) == TimerAction::SelectSeconds);
  assert(timerHitTest(TimerUiState::Ready, 80, 165) == TimerAction::Decrement);
  assert(timerHitTest(TimerUiState::Ready, 160, 165) == TimerAction::Increment);
  assert(timerHitTest(TimerUiState::Ready, 120, 209) == TimerAction::StartPause);
  assert(timerHitTest(TimerUiState::Running, 80, 185) == TimerAction::StartPause);
  assert(timerHitTest(TimerUiState::Paused, 80, 185) == TimerAction::StartPause);
  assert(timerHitTest(TimerUiState::Running, 160, 185) == TimerAction::Reset);
  assert(timerHitTest(TimerUiState::Paused, 160, 185) == TimerAction::Reset);
  for (auto state : {TimerUiState::Ready, TimerUiState::Running,
                     TimerUiState::Paused}) {
    assert(timerHitTest(state, 0, 0) == TimerAction::None);
    assert(timerHitTest(state, -1, 120) == TimerAction::None);
    assert(timerHitTest(state, 240, 120) == TimerAction::None);
    assert(timerHitTest(state, 120, 0) == TimerAction::None);
  }
  assert(timerHitTest(TimerUiState::Paused, 69, 98) == TimerAction::None);
}

void oldContactCannotActivateANewScreen() {
  TimerTouch touch;
  assert(touch.update(TimerUiState::Ready, true, 120, 209, 100) == TimerAction::None);
  lift(touch, 300);
  assert(touch.update(TimerUiState::Ready, true, 120, 209, 400) == TimerAction::None);
  assert(touch.update(TimerUiState::Ready, false, 0, 0, 500) == TimerAction::StartPause);
}

void ordinaryControlsActOnceOnRelease() {
  TimerTouch touch;
  lift(touch);
  assert(touch.update(TimerUiState::Ready, true, 171, 98, 100) == TimerAction::None);
  assert(touch.update(TimerUiState::Ready, true, 173, 100, 1000) == TimerAction::None);
  assert(touch.update(TimerUiState::Ready, false, 0, 0, 1100) == TimerAction::SelectSeconds);
  lift(touch, 1150);
  assert(touch.update(TimerUiState::Ready, true, 120, 209, 1200) == TimerAction::None);
  assert(touch.update(TimerUiState::Ready, true, 120, 209, 2200) == TimerAction::None);
  assert(touch.update(TimerUiState::Ready, false, 0, 0, 2300) == TimerAction::StartPause);
  lift(touch, 2400);
}

void adjustmentTapsActOnRelease() {
  TimerTouch touch;
  lift(touch);
  assert(touch.update(TimerUiState::Ready, true, 160, 165, 100) == TimerAction::None);
  assert(touch.update(TimerUiState::Ready, false, 0, 0, 200) == TimerAction::Increment);
  assert(touch.update(TimerUiState::Ready, true, 80, 165, 300) == TimerAction::None);
  assert(touch.update(TimerUiState::Ready, false, 0, 0, 400) == TimerAction::Decrement);
}

void adjustmentHoldRepeatsAfterADelay() {
  TimerTouch touch;
  lift(touch);
  assert(touch.update(TimerUiState::Ready, true, 160, 165, 100) == TimerAction::None);
  assert(touch.update(TimerUiState::Ready, true, 160, 165, 599) == TimerAction::None);
  assert(touch.update(TimerUiState::Ready, true, 160, 165, 600) == TimerAction::Increment);
  assert(touch.update(TimerUiState::Ready, true, 160, 165, 719) == TimerAction::None);
  assert(touch.update(TimerUiState::Ready, true, 160, 165, 720) == TimerAction::Increment);
  lift(touch, 800); // A repeated hold must not also count as a tap.
  assert(touch.update(TimerUiState::Ready, true, 80, 165, 900) == TimerAction::None);
  assert(touch.update(TimerUiState::Ready, true, 80, 165, 1400) == TimerAction::Decrement);
  lift(touch, 1500);
}

void draggingOutCancelsTheHold() {
  TimerTouch touch;
  lift(touch);
  assert(touch.update(TimerUiState::Ready, true, 160, 165, 100) == TimerAction::None);
  assert(touch.update(TimerUiState::Ready, true, 120, 98, 300) == TimerAction::None);
  assert(touch.update(TimerUiState::Ready, true, 160, 165, 800) == TimerAction::None);
  assert(touch.update(TimerUiState::Ready, true, 120, 209, 900) == TimerAction::None);
  lift(touch, 1000);
}

void movementInsideAButtonCancelsTheTap() {
  TimerTouch touch;
  lift(touch);
  assert(touch.update(TimerUiState::Ready, true, 160, 165, 100) == TimerAction::None);
  assert(touch.update(TimerUiState::Ready, true, 171, 165, 200) == TimerAction::None);
  assert(touch.update(TimerUiState::Ready, true, 160, 165, 700) == TimerAction::None);
  lift(touch, 800);
  assert(touch.update(TimerUiState::Ready, true, 120, 209, 900) == TimerAction::None);
  assert(touch.update(TimerUiState::Ready, true, 120, 198, 1000) == TimerAction::None);
  lift(touch, 1100);
}

void smallFingerJitterStillAllowsATap() {
  TimerTouch touch;
  lift(touch);
  assert(touch.update(TimerUiState::Ready, true, 120, 209, 100) == TimerAction::None);
  assert(touch.update(TimerUiState::Ready, true, 130, 199, 200) == TimerAction::None);
  assert(touch.update(TimerUiState::Ready, false, 0, 0, 300) == TimerAction::StartPause);
}

void swipesAcrossControlsCannotOperateThem() {
  for (const int start_x : {80, 160}) {
    TimerTouch touch;
    lift(touch);
    assert(touch.update(TimerUiState::Ready, true, start_x, 165, 100) == TimerAction::None);
    assert(touch.update(TimerUiState::Ready, true, 120, 165, 200) == TimerAction::None);
    assert(touch.update(TimerUiState::Ready, true, 240 - start_x, 165, 300) == TimerAction::None);
    lift(touch, 400);
  }
  TimerTouch touch;
  lift(touch);
  assert(touch.update(TimerUiState::Running, true, 80, 185, 100) == TimerAction::None);
  assert(touch.update(TimerUiState::Running, true, 160, 185, 300) == TimerAction::None);
  lift(touch, 400, TimerUiState::Running);
}

void stateChangesCancelAnUnreleasedTap() {
  TimerTouch touch;
  lift(touch);
  assert(touch.update(TimerUiState::Running, true, 80, 185, 100) == TimerAction::None);
  assert(touch.update(TimerUiState::Paused, true, 80, 185, 200) == TimerAction::None);
  lift(touch, 300, TimerUiState::Paused);
  assert(touch.update(TimerUiState::Paused, true, 80, 185, 400) == TimerAction::None);
  lift(touch, 500, TimerUiState::Ready); // State change only seen on release.
}

void expiryCannotRestartTheTimerUnderAHeldFinger() {
  TimerTouch touch;
  lift(touch);
  assert(touch.update(TimerUiState::Running, true, 80, 185, 100) == TimerAction::None);
  touch.reset(); // Expiry restores the configured Ready screen.
  assert(touch.update(TimerUiState::Ready, true, 120, 209, 1100) == TimerAction::None);
  lift(touch, 2200);
  assert(touch.update(TimerUiState::Ready, true, 120, 209, 2300) == TimerAction::None);
  assert(touch.update(TimerUiState::Ready, false, 0, 0, 2400) == TimerAction::StartPause);
}

void aFailedSampleNeedsAnotherValidLift() {
  TimerTouch touch;
  lift(touch);
  assert(touch.update(TimerUiState::Ready, true, 160, 165, 100) == TimerAction::None);
  touch.reset(); // Main cancels the contact after an I2C error.
  assert(touch.update(TimerUiState::Ready, true, 160, 165, 1000) == TimerAction::None);
  lift(touch, 1100);
  assert(touch.update(TimerUiState::Ready, true, 160, 165, 1200) == TimerAction::None);
  assert(touch.update(TimerUiState::Ready, false, 0, 0, 1300) == TimerAction::Increment);
}

void aDragIntoAControlDoesNotActivateIt() {
  TimerTouch touch;
  lift(touch);
  assert(touch.update(TimerUiState::Ready, true, 120, 30, 100) == TimerAction::None);
  assert(touch.update(TimerUiState::Ready, true, 120, 209, 200) == TimerAction::None);
  assert(touch.update(TimerUiState::Ready, true, 120, 209, 1000) == TimerAction::None);
  lift(touch, 1100);
}

void leavingAButtonCancelsEvenWithoutMuchMovement() {
  TimerTouch touch;
  lift(touch);
  assert(touch.update(TimerUiState::Ready, true, 113, 165, 100) == TimerAction::None);
  assert(touch.update(TimerUiState::Ready, true, 115, 165, 200) == TimerAction::None);
  assert(touch.update(TimerUiState::Ready, true, 113, 165, 600) == TimerAction::None);
  lift(touch, 700);
}

void movingAfterAHoldStopsFurtherRepeats() {
  TimerTouch touch;
  lift(touch);
  assert(touch.update(TimerUiState::Ready, true, 160, 165, 100) == TimerAction::None);
  assert(touch.update(TimerUiState::Ready, true, 160, 165, 600) == TimerAction::Increment);
  assert(touch.update(TimerUiState::Ready, true, 175, 165, 720) == TimerAction::None);
  assert(touch.update(TimerUiState::Ready, true, 160, 165, 1000) == TimerAction::None);
  lift(touch, 1100);
}

void repeatTimingSurvivesMillisRollover() {
  TimerTouch touch;
  lift(touch);
  const uint32_t start = std::numeric_limits<uint32_t>::max() - 199;
  assert(touch.update(TimerUiState::Ready, true, 160, 165, start) == TimerAction::None);
  assert(touch.update(TimerUiState::Ready, true, 160, 165, 299) == TimerAction::None);
  assert(touch.update(TimerUiState::Ready, true, 160, 165, 300) == TimerAction::Increment);
  assert(touch.update(TimerUiState::Ready, true, 160, 165, 420) == TimerAction::Increment);
  lift(touch, 500);
}

}  // namespace

int main() {
  controlsMatchTheVisibleState();
  oldContactCannotActivateANewScreen();
  ordinaryControlsActOnceOnRelease();
  adjustmentTapsActOnRelease();
  adjustmentHoldRepeatsAfterADelay();
  draggingOutCancelsTheHold();
  movementInsideAButtonCancelsTheTap();
  smallFingerJitterStillAllowsATap();
  swipesAcrossControlsCannotOperateThem();
  stateChangesCancelAnUnreleasedTap();
  expiryCannotRestartTheTimerUnderAHeldFinger();
  aFailedSampleNeedsAnotherValidLift();
  aDragIntoAControlDoesNotActivateIt();
  leavingAButtonCancelsEvenWithoutMuchMovement();
  movingAfterAHoldStopsFurtherRepeats();
  repeatTimingSurvivesMillisRollover();
  std::puts("Timer touch: all 16 scenarios passed.");
}
