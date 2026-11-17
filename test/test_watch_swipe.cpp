#include "WatchSwipe.hpp"

#include <cassert>
#include <cstdio>
#include <initializer_list>
#include <limits>

using watch::SwipeDirection;
using watch::WatchSwipe;

namespace {

void lift(WatchSwipe& swipe, uint32_t now = 0) {
  assert(swipe.update(false, 0, 0, now) == SwipeDirection::None);
  assert(!swipe.active());
  assert(!swipe.dragging());
  assert(swipe.offset() == 0);
}

void horizontalSwipesWaitForRelease() {
  WatchSwipe swipe;
  lift(swipe);
  assert(swipe.update(true, 180, 120, 100) == SwipeDirection::None);
  assert(swipe.active());
  assert(swipe.update(true, 80, 120, 200) == SwipeDirection::None);
  assert(swipe.dragging());
  assert(swipe.offset() == -100);
  assert(swipe.update(false, 0, 0, 250) == SwipeDirection::Left);
  assert(!swipe.active());
  assert(swipe.update(true, 60, 120, 300) == SwipeDirection::None);
  assert(swipe.update(true, 180, 120, 400) == SwipeDirection::None);
  assert(swipe.update(false, 0, 0, 450) == SwipeDirection::Right);
}

void stationaryTapsAndShortDragsDoNotNavigate() {
  WatchSwipe swipe;
  lift(swipe);
  swipe.update(true, 120, 120, 100);
  assert(swipe.update(false, 0, 0, 200) == SwipeDirection::None);
  swipe.update(true, 120, 120, 300);
  swipe.update(true, 164, 120, 400);
  assert(swipe.update(false, 0, 0, 500) == SwipeDirection::None);
}

void exactCommitDistanceIsAccepted() {
  WatchSwipe swipe;
  lift(swipe);
  swipe.update(true, 120, 120, 100);
  swipe.update(true, 75, 120, 899);
  assert(swipe.update(false, 0, 0, 900) == SwipeDirection::Left);
}

void verticalAndDiagonalDragsDoNotNavigate() {
  WatchSwipe swipe;
  lift(swipe);
  swipe.update(true, 120, 40, 100);
  swipe.update(true, 120, 180, 200);
  assert(swipe.update(false, 0, 0, 300) == SwipeDirection::None);
  swipe.update(true, 180, 100, 400);
  swipe.update(true, 130, 130, 500);
  assert(swipe.update(false, 0, 0, 600) == SwipeDirection::None);
}

void mostlyHorizontalDragCanTolerateSomeVerticalMovement() {
  WatchSwipe swipe;
  lift(swipe);
  swipe.update(true, 180, 100, 100);
  swipe.update(true, 110, 135, 200);
  assert(swipe.update(false, 0, 0, 300) == SwipeDirection::Left);
}

void verticalExcursionCannotBeHiddenByReturningToTheStartHeight() {
  WatchSwipe swipe;
  lift(swipe);
  swipe.update(true, 180, 100, 100);
  swipe.update(true, 160, 136, 200);
  swipe.update(true, 80, 100, 300);
  assert(swipe.update(false, 0, 0, 400) == SwipeDirection::None);
}

void slowAndHeldDragsStillFollowTheFinger() {
  WatchSwipe swipe;
  lift(swipe);
  swipe.update(true, 180, 120, 100);
  swipe.update(true, 80, 120, 200);
  swipe.update(true, 80, 120, 5000);
  assert(swipe.dragging());
  assert(swipe.offset() == -100);
  assert(swipe.update(false, 0, 0, 6000) == SwipeDirection::Left);
}

void oneContactEmitsOnlyOneDirection() {
  WatchSwipe swipe;
  lift(swipe);
  swipe.update(true, 180, 120, 100);
  swipe.update(true, 130, 120, 200);
  swipe.update(true, 80, 120, 300);
  assert(swipe.update(false, 0, 0, 400) == SwipeDirection::Left);
  lift(swipe, 500);
  lift(swipe, 600);
}

void releaseCoordinatesAreIgnored() {
  WatchSwipe swipe;
  lift(swipe);
  swipe.update(true, 180, 120, 100);
  swipe.update(true, 80, 120, 200);
  assert(swipe.update(false, -1000, 1000, 300) == SwipeDirection::Left);
}

void initialFingerMustLiftBeforeItCanNavigate() {
  WatchSwipe swipe;
  swipe.update(true, 180, 120, 100);
  swipe.update(true, 80, 120, 200);
  assert(!swipe.active());
  lift(swipe, 300);
  swipe.update(true, 180, 120, 400);
  swipe.update(true, 80, 120, 500);
  assert(swipe.update(false, 0, 0, 600) == SwipeDirection::Left);
}

void resetCancelsAHeldFingerUntilItLifts() {
  WatchSwipe swipe;
  lift(swipe);
  swipe.update(true, 180, 120, 100);
  swipe.reset();
  assert(!swipe.active());
  swipe.update(true, 80, 120, 200);
  lift(swipe, 300);
  swipe.update(true, 180, 120, 400);
  swipe.update(true, 80, 120, 500);
  assert(swipe.update(false, 0, 0, 600) == SwipeDirection::Left);
}

void invalidPressedCoordinatesCancelTheContact() {
  for (int coordinate : {-1, 240}) {
    WatchSwipe swipe;
    lift(swipe);
    swipe.update(true, 180, 120, 100);
    swipe.update(true, coordinate, 120, 150);
    swipe.update(true, 80, 120, 200);
    lift(swipe, 300);
    swipe.update(true, 180, 120, 400);
    swipe.update(true, 80, coordinate, 450);
    swipe.update(true, 80, 120, 500);
    lift(swipe, 600);
  }
}

void timestampRolloverDoesNotLimitDragging() {
  WatchSwipe swipe;
  lift(swipe);
  const uint32_t start = std::numeric_limits<uint32_t>::max() - 199U;
  swipe.update(true, 180, 120, start);
  swipe.update(true, 80, 120, 100);
  assert(swipe.update(false, 0, 0, 200) == SwipeDirection::Left);
  swipe.update(true, 180, 120, start);
  swipe.update(true, 80, 120, 100);
  assert(swipe.update(false, 0, 0, 601) == SwipeDirection::Left);
}

void timerHoldCanClaimContactWithoutALaterSwipe() {
  WatchSwipe swipe;
  lift(swipe);
  swipe.update(true, 160, 165, 100);
  swipe.update(true, 160, 165, 600);
  swipe.reset(); // The timer adjustment has begun to repeat.
  swipe.update(true, 80, 165, 650);
  lift(swipe, 700);
}

void intentThresholdLeavesSmallJitterForTaps() {
  WatchSwipe swipe;
  lift(swipe);
  swipe.update(true, 120, 120, 100);
  swipe.update(true, 131, 120, 120);
  assert(!swipe.dragging());
  assert(swipe.offset() == 0);
  swipe.update(true, 132, 120, 140);
  assert(swipe.dragging());
  assert(swipe.offset() == 12);
  assert(swipe.update(false, 0, 0, 160) == SwipeDirection::None);
}

void draggingCanReverseToEitherNeighbour() {
  WatchSwipe swipe;
  lift(swipe);
  swipe.update(true, 120, 120, 100);
  swipe.update(true, 80, 120, 200);
  assert(swipe.offset() == -40);
  swipe.update(true, 120, 120, 250);
  assert(swipe.dragging());
  assert(swipe.offset() == 0);
  swipe.update(true, 170, 120, 300);
  assert(swipe.offset() == 50);
  assert(swipe.update(false, 0, 0, 400) == SwipeDirection::Right);
}

void returningToTheOriginCancelsNavigation() {
  WatchSwipe swipe;
  lift(swipe);
  swipe.update(true, 180, 120, 100);
  swipe.update(true, 80, 120, 200);
  swipe.update(true, 180, 120, 300);
  assert(swipe.dragging());
  assert(swipe.offset() == 0);
  assert(swipe.update(false, 0, 0, 400) == SwipeDirection::None);
}

void horizontalLockToleratesLaterVerticalDrift() {
  WatchSwipe swipe;
  lift(swipe);
  swipe.update(true, 180, 100, 100);
  swipe.update(true, 165, 100, 120);
  assert(swipe.dragging());
  swipe.update(true, 80, 200, 200);
  assert(swipe.dragging());
  assert(swipe.offset() == -100);
  assert(swipe.update(false, 0, 0, 300) == SwipeDirection::Left);
}

void verticalIntentCannotLaterBecomeAHorizontalDrag() {
  WatchSwipe swipe;
  lift(swipe);
  swipe.update(true, 120, 100, 100);
  swipe.update(true, 125, 112, 120);
  assert(!swipe.dragging());
  swipe.update(true, 220, 100, 200);
  assert(!swipe.dragging());
  assert(swipe.offset() == 0);
  assert(swipe.update(false, 0, 0, 300) == SwipeDirection::None);
}

void invalidCoordinateCancelsAnAlreadyLockedDrag() {
  WatchSwipe swipe;
  lift(swipe);
  swipe.update(true, 180, 120, 100);
  swipe.update(true, 80, 120, 200);
  assert(swipe.dragging());
  swipe.update(true, 240, 120, 250);
  assert(!swipe.dragging());
  assert(swipe.offset() == 0);
  swipe.update(true, 40, 120, 300);
  lift(swipe, 400);
}

}  // namespace

int main() {
  horizontalSwipesWaitForRelease();
  stationaryTapsAndShortDragsDoNotNavigate();
  exactCommitDistanceIsAccepted();
  verticalAndDiagonalDragsDoNotNavigate();
  mostlyHorizontalDragCanTolerateSomeVerticalMovement();
  verticalExcursionCannotBeHiddenByReturningToTheStartHeight();
  slowAndHeldDragsStillFollowTheFinger();
  oneContactEmitsOnlyOneDirection();
  releaseCoordinatesAreIgnored();
  initialFingerMustLiftBeforeItCanNavigate();
  resetCancelsAHeldFingerUntilItLifts();
  invalidPressedCoordinatesCancelTheContact();
  timestampRolloverDoesNotLimitDragging();
  timerHoldCanClaimContactWithoutALaterSwipe();
  intentThresholdLeavesSmallJitterForTaps();
  draggingCanReverseToEitherNeighbour();
  returningToTheOriginCancelsNavigation();
  horizontalLockToleratesLaterVerticalDrift();
  verticalIntentCannotLaterBecomeAHorizontalDrag();
  invalidCoordinateCancelsAnAlreadyLockedDrag();
  std::puts("Watch swipe: all 20 scenarios passed.");
}
