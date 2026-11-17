#include "WatchfaceTouch.hpp"

#include <cassert>
#include <cstdio>

using watch::SwipeDirection;
using watch::TimerAction;
using watch::TimerUiState;
using watch::WatchfaceTouch;
using watch::WatchfaceTouchResult;

namespace {

WatchfaceTouchResult sample(WatchfaceTouch& touch, bool pressed, int x, int y,
                           uint32_t now) {
  const auto result = touch.update(true, TimerUiState::Ready, pressed, x, y, now);
  assert(result.timer_action == TimerAction::None);
  return result;
}

void lift(WatchfaceTouch& touch, uint32_t now = 0) {
  const auto result = sample(touch, false, 0, 0, now);
  assert(!result.open_settings && !result.dragging && !result.drag_released);
  assert(!result.settings_dragging && !result.settings_released);
}

void topPullOpensOnlyWhenReleased() {
  WatchfaceTouch touch;
  lift(touch);
  auto result = sample(touch, true, 120, 20, 100);
  assert(!result.open_settings && !result.settings_dragging);
  result = sample(touch, true, 120, 40, 120);
  assert(!result.open_settings && !result.dragging);
  assert(result.settings_dragging && result.settings_offset == 20);
  result = sample(touch, true, 120, 100, 160);
  assert(!result.open_settings && !result.dragging);
  assert(result.settings_dragging && result.settings_offset == 80);
  result = sample(touch, false, 0, 0, 200);
  assert(result.open_settings && result.swipe == SwipeDirection::None);
  assert(result.settings_released && !result.settings_dragging);
  assert(result.settings_offset == 80);
}

void repeatedReleaseCannotOpenAgain() {
  WatchfaceTouch touch;
  lift(touch);
  sample(touch, true, 120, 20, 100);
  sample(touch, true, 120, 100, 160);
  assert(sample(touch, false, 0, 0, 200).open_settings);
  lift(touch, 220);
  lift(touch, 240);
}

void upperBandBoundaryIsInclusive() {
  WatchfaceTouch touch;
  lift(touch);
  sample(touch, true, 120, 50, 100);
  sample(touch, true, 120, 95, 160);
  assert(sample(touch, false, 0, 0, 200).open_settings);
}

void startingBelowUpperBandCannotOpen() {
  WatchfaceTouch touch;
  lift(touch);
  sample(touch, true, 120, 51, 100);
  sample(touch, true, 120, 20, 140);
  sample(touch, true, 120, 120, 180);
  assert(!sample(touch, false, 0, 0, 220).open_settings);
}

void timerDigitDragCannotOpenSettings() {
  WatchfaceTouch touch;
  lift(touch);
  sample(touch, true, 171, 98, 100);
  sample(touch, true, 171, 180, 160);
  const auto result = sample(touch, false, 0, 0, 200);
  assert(!result.open_settings && result.swipe == SwipeDirection::None);
}

void upwardIntentCannotReverseIntoMenuOpening() {
  WatchfaceTouch touch;
  lift(touch);
  sample(touch, true, 120, 40, 100);
  sample(touch, true, 120, 20, 120);
  sample(touch, true, 120, 120, 180);
  const auto result = sample(touch, false, 0, 0, 220);
  assert(!result.open_settings && result.swipe == SwipeDirection::None);
}

void subthresholdPullIsConsumedWithoutOpening() {
  WatchfaceTouch touch;
  lift(touch);
  sample(touch, true, 120, 20, 100);
  sample(touch, true, 120, 64, 160);
  const auto result = sample(touch, false, 0, 0, 200);
  assert(!result.open_settings && !result.drag_released);
  assert(result.settings_released && result.settings_offset == 44);
  assert(result.swipe == SwipeDirection::None);
}

void verticalFirstCannotBecomeCarouselDrag() {
  WatchfaceTouch touch;
  lift(touch);
  sample(touch, true, 120, 20, 100);
  sample(touch, true, 120, 45, 140);
  auto result = sample(touch, true, 220, 100, 180);
  assert(!result.dragging);
  result = sample(touch, false, 0, 0, 220);
  assert(!result.open_settings && !result.drag_released);
  assert(result.swipe == SwipeDirection::None);
}

void horizontalFirstKeepsCarouselOwnership() {
  WatchfaceTouch touch;
  lift(touch);
  sample(touch, true, 120, 20, 100);
  auto result = sample(touch, true, 140, 20, 120);
  assert(result.dragging && result.drag_offset == 20);
  result = sample(touch, true, 180, 100, 180);
  assert(result.dragging && result.drag_offset == 60);
  result = sample(touch, false, 0, 0, 220);
  assert(!result.open_settings && result.drag_released);
  assert(result.swipe == SwipeDirection::Right);
}

void leftwardFirstKeepsCarouselOwnership() {
  WatchfaceTouch touch;
  lift(touch);
  sample(touch, true, 120, 20, 100);
  auto result = sample(touch, true, 100, 20, 120);
  assert(result.dragging && result.drag_offset == -20);
  result = sample(touch, true, 60, 100, 180);
  assert(result.dragging && result.drag_offset == -60);
  result = sample(touch, false, 0, 0, 220);
  assert(!result.open_settings && result.drag_released);
  assert(result.swipe == SwipeDirection::Left);
}

void resetRequiresLiftBeforeOpening() {
  WatchfaceTouch touch;
  lift(touch);
  sample(touch, true, 120, 20, 100);
  sample(touch, true, 120, 45, 140);
  touch.reset();
  sample(touch, true, 120, 100, 180);
  lift(touch, 220);
  sample(touch, true, 120, 20, 260);
  sample(touch, true, 120, 100, 300);
  assert(sample(touch, false, 0, 0, 340).open_settings);
}

void invalidPointCancelsThePullUntilLift() {
  WatchfaceTouch touch;
  lift(touch);
  sample(touch, true, 120, 20, 100);
  sample(touch, true, 120, 45, 140);
  assert(!sample(touch, true, 240, 100, 160).open_settings);
  sample(touch, true, 120, 100, 180);
  lift(touch, 220);
}

void pullingAcrossTimerStartCannotActivateIt() {
  WatchfaceTouch touch;
  lift(touch);
  sample(touch, true, 120, 20, 100);
  sample(touch, true, 120, 100, 160);
  sample(touch, true, 120, 209, 180);
  assert(sample(touch, false, 0, 0, 220).open_settings);
}

void releasedPullMustRemainVerticallyDominant() {
  WatchfaceTouch touch;
  lift(touch);
  sample(touch, true, 120, 20, 100);
  sample(touch, true, 120, 40, 120);
  sample(touch, true, 149, 80, 160);
  assert(sample(touch, false, 0, 0, 200).open_settings);

  sample(touch, true, 120, 20, 240);
  sample(touch, true, 120, 40, 260);
  sample(touch, true, 151, 80, 300);
  assert(!sample(touch, false, 0, 0, 340).open_settings);
}

void canceledPullRearmsControlsForNextContact() {
  WatchfaceTouch touch;
  lift(touch);
  sample(touch, true, 120, 20, 100);
  sample(touch, true, 120, 50, 140);
  sample(touch, true, 120, 25, 180);
  assert(!sample(touch, false, 0, 0, 220).open_settings);
  sample(touch, true, 120, 209, 260);
  const auto result = touch.update(true, TimerUiState::Ready, false, 0, 0, 300);
  assert(result.timer_action == TimerAction::StartPause && !result.open_settings);
}

void oldFingerCannotStartPullAfterWake() {
  WatchfaceTouch touch;
  sample(touch, true, 120, 20, 100);
  sample(touch, true, 120, 100, 160);
  lift(touch, 200);
}

void adjustmentHoldCannotBecomePullDown() {
  WatchfaceTouch touch;
  lift(touch);
  sample(touch, true, 160, 165, 100);
  auto result = touch.update(true, TimerUiState::Ready, true, 160, 165, 600);
  assert(result.timer_action == TimerAction::Increment && !result.open_settings);
  sample(touch, true, 120, 20, 650);
  sample(touch, true, 120, 100, 700);
  result = sample(touch, false, 0, 0, 740);
  assert(!result.open_settings && !result.dragging);
  assert(result.swipe == SwipeDirection::None);
}

void pullFollowsFingerAtTheIntentThreshold() {
  WatchfaceTouch touch;
  lift(touch);
  sample(touch, true, 120, 20, 100);
  auto result = sample(touch, true, 120, 31, 120);
  assert(!result.settings_dragging && result.settings_offset == 0);
  result = sample(touch, true, 126, 32, 140);
  assert(result.settings_dragging && result.settings_offset == 12);
  result = sample(touch, true, 126, 33, 160);
  assert(result.settings_dragging && result.settings_offset == 13);
}

void reversedPullClampsAtTheOriginAndKeepsOwnership() {
  WatchfaceTouch touch;
  lift(touch);
  sample(touch, true, 120, 40, 100);
  sample(touch, true, 120, 80, 140);
  auto result = sample(touch, true, 180, 20, 180);
  assert(result.settings_dragging && result.settings_offset == 0);
  assert(!result.dragging && result.swipe == SwipeDirection::None);
  result = sample(touch, false, 0, 0, 220);
  assert(result.settings_released && result.settings_offset == 0);
  assert(!result.open_settings && !result.drag_released);
}

void reversedPullCanFollowBackDownBeforeRelease() {
  WatchfaceTouch touch;
  lift(touch);
  sample(touch, true, 120, 40, 100);
  sample(touch, true, 120, 80, 140);
  sample(touch, true, 120, 20, 180);
  auto result = sample(touch, true, 120, 110, 220);
  assert(result.settings_dragging && result.settings_offset == 70);
  result = sample(touch, false, 0, 0, 260);
  assert(result.settings_released && result.open_settings);
  assert(result.settings_offset == 70);
}

void canceledDiagonalPullRetainsItsReleaseOffset() {
  WatchfaceTouch touch;
  lift(touch);
  sample(touch, true, 120, 20, 100);
  sample(touch, true, 120, 40, 140);
  auto result = sample(touch, true, 160, 80, 180);
  assert(result.settings_dragging && result.settings_offset == 60);
  result = sample(touch, false, 0, 0, 220);
  assert(result.settings_released && result.settings_offset == 60);
  assert(!result.open_settings);
}

void aResetCannotEmitTheAbortedPullRelease() {
  WatchfaceTouch touch;
  lift(touch);
  sample(touch, true, 120, 20, 100);
  assert(sample(touch, true, 120, 80, 140).settings_dragging);
  touch.reset();
  auto result = sample(touch, true, 120, 100, 180);
  assert(!result.settings_dragging && !result.settings_released);
  lift(touch, 220);
}

}  // namespace

int main() {
  topPullOpensOnlyWhenReleased();
  repeatedReleaseCannotOpenAgain();
  upperBandBoundaryIsInclusive();
  startingBelowUpperBandCannotOpen();
  timerDigitDragCannotOpenSettings();
  upwardIntentCannotReverseIntoMenuOpening();
  subthresholdPullIsConsumedWithoutOpening();
  verticalFirstCannotBecomeCarouselDrag();
  horizontalFirstKeepsCarouselOwnership();
  leftwardFirstKeepsCarouselOwnership();
  resetRequiresLiftBeforeOpening();
  invalidPointCancelsThePullUntilLift();
  pullingAcrossTimerStartCannotActivateIt();
  releasedPullMustRemainVerticallyDominant();
  canceledPullRearmsControlsForNextContact();
  oldFingerCannotStartPullAfterWake();
  adjustmentHoldCannotBecomePullDown();
  pullFollowsFingerAtTheIntentThreshold();
  reversedPullClampsAtTheOriginAndKeepsOwnership();
  reversedPullCanFollowBackDownBeforeRelease();
  canceledDiagonalPullRetainsItsReleaseOffset();
  aResetCannotEmitTheAbortedPullRelease();
  std::puts("Settings pull-down: all 22 scenarios passed.");
}
