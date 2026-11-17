#include "SettingsSlide.hpp"

#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>

namespace {
unsigned allocation_count = 0;
}

void* operator new(std::size_t size) {
  ++allocation_count;
  void* result = std::malloc(size == 0 ? 1 : size);
  if (!result) throw std::bad_alloc();
  return result;
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { ::operator delete(pointer); }

using watch::SettingsSlide;
using watch::SettingsSlideFrame;

namespace {

void assertFrame(const SettingsSlideFrame& frame, int visibility, bool finished) {
  assert(frame.settings_y == visibility - 240);
  assert(frame.watchface_y == visibility);
  assert(frame.finished == finished);
}

void idleIgnoresUpdates() {
  SettingsSlide slide;
  assert(!slide.active());
  assert(!slide.dragging());
  assert(!slide.due(0));
  slide.drag(100, 0);
  slide.release(true, 0);
  slide.presented(0);
  assert(!slide.active());
  assert(!slide.due(100));
  assertFrame(slide.frame(100), 0, true);
}

void openingBeginsAtClosedEndpoint() {
  SettingsSlide slide;
  slide.begin(false, 100);
  assert(slide.active());
  assert(slide.dragging());
  assert(!slide.originOpen());
  assert(!slide.targetOpen());
  assert(slide.due(100));
  assertFrame(slide.frame(100), 0, false);
}

void closingBeginsAtOpenEndpoint() {
  SettingsSlide slide;
  slide.begin(true, 100);
  assert(slide.originOpen());
  assert(slide.targetOpen());
  assertFrame(slide.frame(100), 240, false);
}

void dragUsesOriginRatherThanAccumulatedDeltas() {
  SettingsSlide slide;
  slide.begin(false, 0);
  slide.drag(70, 1);
  assertFrame(slide.frame(1), 70, false);
  slide.drag(100, 2);
  assertFrame(slide.frame(2), 100, false);
  slide.drag(20, 3);
  assertFrame(slide.frame(3), 20, false);
  slide.begin(true, 4);
  slide.drag(-100, 5);
  assertFrame(slide.frame(5), 140, false);
  slide.drag(-20, 6);
  assertFrame(slide.frame(6), 220, false);
}

void dragClampsBothOriginsAndExtremeInput() {
  SettingsSlide slide;
  for (int origin = 0; origin < 2; ++origin) {
    slide.begin(origin != 0, 0);
    slide.drag(std::numeric_limits<int>::max(), 1);
    assertFrame(slide.frame(1), 240, false);
    slide.drag(std::numeric_limits<int>::min(), 2);
    assertFrame(slide.frame(2), 0, false);
  }
}

void reversalRetainsOriginAndTargetMetadata() {
  SettingsSlide slide;
  slide.begin(false, 0);
  slide.drag(200, 10);
  slide.drag(5, 20);
  slide.drag(-30, 30);
  assertFrame(slide.frame(30), 0, false);
  assert(!slide.originOpen());
  assert(!slide.targetOpen());
  slide.begin(true, 40);
  slide.drag(-200, 50);
  slide.drag(40, 60);
  assertFrame(slide.frame(60), 240, false);
  assert(slide.originOpen());
  assert(slide.targetOpen());
}

void openingCommitsFromLivePosition() {
  SettingsSlide slide;
  slide.begin(false, 0);
  slide.drag(60, 90);
  slide.release(true, 100);
  assert(!slide.dragging());
  assert(!slide.originOpen());
  assert(slide.targetOpen());
  assertFrame(slide.frame(100), 60, false);
  assertFrame(slide.frame(249), 240, false);
  assertFrame(slide.frame(250), 240, true);
}

void closingCommitsFromLivePosition() {
  SettingsSlide slide;
  slide.begin(true, 0);
  slide.drag(-60, 90);
  slide.release(true, 100);
  assert(slide.originOpen());
  assert(!slide.targetOpen());
  assertFrame(slide.frame(100), 180, false);
  assertFrame(slide.frame(250), 0, true);
}

void cancelledOpeningReturnsToClosedOrigin() {
  SettingsSlide slide;
  slide.begin(false, 0);
  slide.drag(120, 90);
  slide.release(false, 100);
  assert(!slide.targetOpen());
  assertFrame(slide.frame(100), 120, false);
  assertFrame(slide.frame(200), 0, true);
}

void cancelledClosingReturnsToOpenOrigin() {
  SettingsSlide slide;
  slide.begin(true, 0);
  slide.drag(-120, 90);
  slide.release(false, 100);
  assert(slide.targetOpen());
  assertFrame(slide.frame(100), 120, false);
  assertFrame(slide.frame(200), 240, true);
}

void cubicEasingHasExactSamplePositions() {
  SettingsSlide slide;
  slide.begin(false, 0);
  slide.release(true, 100);
  assertFrame(slide.frame(100), 0, false);
  assertFrame(slide.frame(150), 139, false);
  assertFrame(slide.frame(200), 210, false);
  assertFrame(slide.frame(250), 236, false);
  assertFrame(slide.frame(300), 240, true);
}

void geometryIsAdjacentAndMotionMonotonic() {
  for (int origin = 0; origin < 2; ++origin) {
    for (int commit = 0; commit < 2; ++commit) {
      SettingsSlide slide;
      slide.begin(origin != 0, 0);
      slide.drag(origin ? -97 : 97, 10);
      slide.release(commit != 0, 100);
      int previous = slide.frame(100).watchface_y;
      const bool increasing = slide.targetOpen();
      for (uint32_t elapsed = 0; elapsed <= 300; ++elapsed) {
        const auto frame = slide.frame(100 + elapsed);
        assert(frame.settings_y + 240 == frame.watchface_y);
        assert(frame.watchface_y >= 0 && frame.watchface_y <= 240);
        assert(increasing ? frame.watchface_y >= previous
                          : frame.watchface_y <= previous);
        previous = frame.watchface_y;
      }
      assertFrame(slide.frame(400), increasing ? 240 : 0, true);
    }
  }
}

void dragCadenceIsNotRestartedByInput() {
  SettingsSlide slide;
  slide.begin(false, 100);
  slide.presented(100);
  for (uint32_t now = 101; now < 116; ++now) {
    slide.drag(static_cast<int>(now - 100), now);
    assert(!slide.due(now));
  }
  slide.drag(16, 116);
  assert(slide.due(116));
  slide.presented(116);
  slide.drag(17, 117);
  assert(!slide.due(131));
  assert(slide.due(132));
}

void heldFingerDoesNotScheduleRepeatedFramesOrTimeout() {
  SettingsSlide slide;
  slide.begin(false, 0);
  slide.drag(80, 10);
  assert(slide.due(10));
  slide.presented(10);
  slide.drag(80, 100);
  assert(!slide.due(100));
  assert(!slide.due(1000000));
  assertFrame(slide.frame(1000000), 80, false);
  slide.drag(81, 1000001);
  assert(slide.due(1000001));
}

void returnToPresentedPositionNeedsNoFrame() {
  SettingsSlide slide;
  slide.begin(false, 0);
  slide.drag(60, 1);
  slide.presented(1);
  slide.drag(70, 2);
  slide.drag(60, 3);
  assert(!slide.due(100));
}

void releaseIsImmediateWithoutPositionJump() {
  SettingsSlide slide;
  slide.begin(false, 0);
  slide.drag(100, 5);
  slide.presented(5);
  slide.drag(101, 6);
  slide.release(true, 6);
  assert(slide.due(6));
  assertFrame(slide.frame(6), 101, false);
  slide.presented(6);
  assert(!slide.due(21));
  assert(slide.due(22));
}

void minimumDurationAndRoundingAreApplied() {
  SettingsSlide slide;
  slide.begin(false, 0);
  slide.drag(239, 0);
  slide.release(true, 100);
  assert(!slide.frame(159).finished);
  assertFrame(slide.frame(160), 240, true);
  slide.begin(false, 200);
  slide.drag(167, 200);  // Remaining 73px takes ceil(73*200/240) = 61ms.
  slide.release(true, 200);
  assert(!slide.frame(260).finished);
  assertFrame(slide.frame(261), 240, true);
}

void zeroDistanceFinishesImmediately() {
  for (int origin = 0; origin < 2; ++origin) {
    for (int commit = 0; commit < 2; ++commit) {
      SettingsSlide slide;
      slide.begin(origin != 0, 0);
      if (commit) slide.drag(origin ? -240 : 240, 1);
      slide.release(commit != 0, 10);
      assert(slide.due(10));
      assertFrame(slide.frame(10), slide.targetOpen() ? 240 : 0, true);
    }
  }
}

void completionBypassesCadenceAndMissedFramesDoNotDelayIt() {
  SettingsSlide slide;
  slide.begin(false, 0);
  slide.release(true, 100);
  slide.presented(295);
  assert(!slide.due(299));
  assert(slide.due(300));
  assertFrame(slide.frame(300), 240, true);
  assertFrame(slide.frame(10000), 240, true);
  assert(slide.active());  // Caller acknowledges completion with cancel().
}

void cancellationAndRestartResetPresentationState() {
  SettingsSlide slide;
  slide.begin(false, 0);
  slide.drag(100, 10);
  slide.release(true, 20);
  slide.presented(30);
  slide.cancel();
  assert(!slide.active());
  assert(!slide.due(1000));
  assert(!slide.originOpen());
  assert(slide.targetOpen());
  slide.drag(200, 1000);
  slide.release(false, 1000);
  assert(!slide.active());
  slide.begin(true, 1001);
  assert(slide.dragging());
  assert(slide.due(1001));
  assert(slide.originOpen() && slide.targetOpen());
  assertFrame(slide.frame(1001), 240, false);
}

void beginCanInterruptDragAndSettling() {
  SettingsSlide slide;
  slide.begin(false, 0);
  slide.drag(100, 10);
  slide.begin(true, 11);
  assertFrame(slide.frame(11), 240, false);
  slide.drag(-100, 12);
  slide.release(true, 13);
  slide.begin(false, 14);
  assertFrame(slide.frame(14), 0, false);
  assert(!slide.originOpen() && !slide.targetOpen());
  assert(slide.due(14));
}

void rolloverPreservesCadenceAndSettleDuration() {
  const uint32_t maximum = std::numeric_limits<uint32_t>::max();
  SettingsSlide slide;
  slide.begin(false, maximum - 8U);
  slide.presented(maximum - 8U);
  slide.drag(100, maximum - 3U);
  assert(!slide.due(6));
  assert(slide.due(7));
  slide.begin(false, maximum - 59U);
  slide.drag(100, maximum - 50U);
  slide.release(true, maximum - 49U);  // 140px -> 117ms, crossing rollover.
  assertFrame(slide.frame(maximum - 49U), 100, false);
  assert(!slide.frame(66).finished);
  assertFrame(slide.frame(67), 240, true);
}

void operationsDoNotAllocate() {
  const auto before = allocation_count;
  SettingsSlide slide;
  for (int cycle = 0; cycle < 100; ++cycle) {
    slide.begin((cycle % 2) != 0, 0);
    for (int offset = -240; offset <= 240; ++offset) {
      slide.drag(offset, static_cast<uint32_t>(offset + 240));
      if (slide.due(500)) slide.presented(500);
      (void)slide.frame(500);
    }
    slide.release((cycle % 3) != 0, 600);
    for (uint32_t now = 600; now <= 900; ++now) {
      if (slide.due(now)) slide.presented(now);
      (void)slide.frame(now);
    }
    slide.cancel();
  }
  assert(allocation_count == before);
}

}  // namespace

int main() {
  idleIgnoresUpdates();
  openingBeginsAtClosedEndpoint();
  closingBeginsAtOpenEndpoint();
  dragUsesOriginRatherThanAccumulatedDeltas();
  dragClampsBothOriginsAndExtremeInput();
  reversalRetainsOriginAndTargetMetadata();
  openingCommitsFromLivePosition();
  closingCommitsFromLivePosition();
  cancelledOpeningReturnsToClosedOrigin();
  cancelledClosingReturnsToOpenOrigin();
  cubicEasingHasExactSamplePositions();
  geometryIsAdjacentAndMotionMonotonic();
  dragCadenceIsNotRestartedByInput();
  heldFingerDoesNotScheduleRepeatedFramesOrTimeout();
  returnToPresentedPositionNeedsNoFrame();
  releaseIsImmediateWithoutPositionJump();
  minimumDurationAndRoundingAreApplied();
  zeroDistanceFinishesImmediately();
  completionBypassesCadenceAndMissedFramesDoNotDelayIt();
  cancellationAndRestartResetPresentationState();
  beginCanInterruptDragAndSettling();
  rolloverPreservesCadenceAndSettleDuration();
  operationsDoNotAllocate();
  std::puts("Settings slide tests passed (23 cases).");
}
