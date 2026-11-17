#include "WatchfaceSlide.hpp"

#include <cassert>
#include <cstdio>
#include <initializer_list>
#include <limits>

using watch::SlideFrame;
using watch::SwipeDirection;
using watch::WatchfaceSlide;

namespace {

void assertFrame(const SlideFrame& frame, int outgoing, int incoming,
                 bool finished) {
  assert(frame.outgoing_x == outgoing);
  assert(frame.incoming_x == incoming);
  assert(frame.finished == finished);
}

void idleDoesNotScheduleOrRender() {
  WatchfaceSlide slide;
  assert(!slide.active());
  assert(!slide.due(0));
  slide.presented(100);
  assert(!slide.due(200));
  assertFrame(slide.frame(200), 0, 0, true);
}

void leftSlideStartsAndEndsAtExactPositions() {
  WatchfaceSlide slide;
  slide.start(SwipeDirection::Left, 100);
  assert(slide.active());
  assert(slide.due(100));
  assertFrame(slide.frame(100), 0, 240, false);
  assertFrame(slide.frame(300), -240, 0, true);
  assert(slide.active()); // Completion is acknowledged by the rendering loop.
  slide.cancel();
  assert(!slide.active());
}

void rightSlideStartsAndEndsAtExactPositions() {
  WatchfaceSlide slide;
  slide.start(SwipeDirection::Right, 100);
  assertFrame(slide.frame(100), 0, -240, false);
  assertFrame(slide.frame(300), 240, 0, true);
}

void cubicEasingMovesQuicklyThenSettles() {
  WatchfaceSlide slide;
  slide.start(SwipeDirection::Left, 0);
  assertFrame(slide.frame(50), -139, 101, false);
  assertFrame(slide.frame(100), -210, 30, false);
  assertFrame(slide.frame(150), -236, 4, false);
}

void facesStayAdjacentAndMotionIsMonotonic() {
  for (auto direction : {SwipeDirection::Left, SwipeDirection::Right}) {
    WatchfaceSlide slide;
    slide.start(direction, 100);
    int previous = 0;
    for (uint32_t elapsed = 0; elapsed <= 200; ++elapsed) {
      const auto frame = slide.frame(100 + elapsed);
      const int progress = direction == SwipeDirection::Left
          ? -frame.outgoing_x : frame.outgoing_x;
      assert(progress >= previous);
      assert(progress >= 0 && progress <= 240);
      assert(frame.incoming_x - frame.outgoing_x ==
             (direction == SwipeDirection::Left ? 240 : -240));
      assert(frame.finished == (elapsed == 200));
      previous = progress;
    }
  }
}

void cadenceStartsWithAnImmediateFrame() {
  WatchfaceSlide slide;
  slide.start(SwipeDirection::Left, 100);
  assert(slide.due(100));
  slide.presented(100);
  assert(!slide.due(100));
  assert(!slide.due(115));
  assert(slide.due(116));
  slide.presented(120);
  assert(!slide.due(135));
  assert(slide.due(136));
}

void completionDoesNotWaitForAnotherCadenceInterval() {
  WatchfaceSlide slide;
  slide.start(SwipeDirection::Left, 100);
  slide.presented(295);
  assert(!slide.due(299));
  assert(slide.due(300));
  assertFrame(slide.frame(300), -240, 0, true);
}

void missedFramesDoNotLengthenAnimation() {
  WatchfaceSlide slide;
  slide.start(SwipeDirection::Right, 100);
  slide.presented(100);
  assert(slide.due(1100));
  assertFrame(slide.frame(1100), 240, 0, true);
}

void timingSurvivesMillisRollover() {
  WatchfaceSlide slide;
  const uint32_t start = std::numeric_limits<uint32_t>::max() - 99U;
  slide.start(SwipeDirection::Left, start);
  assertFrame(slide.frame(0), -210, 30, false);
  slide.presented(std::numeric_limits<uint32_t>::max() - 4U);
  assert(!slide.due(10));
  assert(slide.due(11));
  assert(!slide.frame(99).finished);
  assert(slide.due(100));
  assertFrame(slide.frame(100), -240, 0, true);
}

void cancelAndRestartUseTheNewDirectionAndTime() {
  WatchfaceSlide slide;
  slide.start(SwipeDirection::Left, 100);
  slide.presented(150);
  slide.cancel();
  assert(!slide.active());
  assert(!slide.due(1000));
  slide.start(SwipeDirection::Right, 2000);
  assert(slide.active());
  assert(slide.due(2000));
  assertFrame(slide.frame(2000), 0, -240, false);
  assertFrame(slide.frame(2100), 210, -30, false);
}

void noDirectionCancelsAnExistingSlide() {
  WatchfaceSlide slide;
  slide.start(SwipeDirection::Left, 100);
  slide.start(SwipeDirection::None, 150);
  assert(!slide.active());
  assert(!slide.due(200));
}

void customAndInvalidWidthsAreSafe() {
  WatchfaceSlide slide;
  slide.start(SwipeDirection::Right, 100);
  assertFrame(slide.frame(100, 320), 0, -320, false);
  assertFrame(slide.frame(200, 320), 280, -40, false);
  assertFrame(slide.frame(300, 320), 320, 0, true);
  assertFrame(slide.frame(150, 0), 0, 0, true);
  assertFrame(slide.frame(150, -1), 0, 0, true);
}

void dragTracksTheFingerWithoutFinishing() {
  WatchfaceSlide slide;
  slide.drag(-35, 100);
  assert(slide.active());
  assert(slide.dragging());
  assert(slide.due(100));
  assertFrame(slide.frame(100), -35, 205, false);
  assertFrame(slide.frame(1000000), -35, 205, false);
  slide.drag(-110, 200);
  assertFrame(slide.frame(200), -110, 130, false);
  slide.drag(60, 300);
  assertFrame(slide.frame(300), 60, -180, false);
}

void stationaryFingerDoesNotRedraw() {
  WatchfaceSlide slide;
  slide.drag(-35, 100);
  slide.presented(100);
  assert(!slide.due(116));
  assert(!slide.due(1000000));
  slide.drag(-35, 1000000);
  assert(!slide.due(1000000));
}

void dragSamplesDoNotRestartFrameCadence() {
  WatchfaceSlide slide;
  slide.drag(-20, 100);
  slide.presented(100);
  slide.drag(-30, 105);
  assert(!slide.due(105));
  slide.drag(-40, 110);
  assert(!slide.due(115));
  slide.drag(-50, 116);
  assert(slide.due(116));
  assertFrame(slide.frame(116), -50, 190, false);
  slide.presented(116);
  assert(!slide.due(132));
}

void returningToPresentedOffsetNeedsNoFrame() {
  WatchfaceSlide slide;
  slide.drag(-20, 100);
  slide.presented(100);
  slide.drag(-40, 105);
  slide.drag(-20, 110);
  assert(!slide.due(116));
}

void reversalRetainsSideUntilItCrossesZero() {
  WatchfaceSlide slide;
  slide.drag(50, 100);
  assertFrame(slide.frame(100), 50, -190, false);
  slide.drag(0, 120);
  assertFrame(slide.frame(120), 0, -240, false);
  slide.drag(-30, 140);
  assertFrame(slide.frame(140), -30, 210, false);
  slide.drag(0, 160);
  assertFrame(slide.frame(160), 0, 240, false);
}

void dragOffsetsClampAtEitherEdge() {
  WatchfaceSlide slide;
  slide.drag(std::numeric_limits<int>::min(), 100);
  assertFrame(slide.frame(100), -240, 0, false);
  slide.drag(std::numeric_limits<int>::max(), 120);
  assertFrame(slide.frame(120), 240, 0, false);
  slide.drag(-400, 140, 320);
  assertFrame(slide.frame(140, 320), -320, 0, false);
}

void releaseCommitsFromCurrentOffsetWithoutJumping() {
  WatchfaceSlide slide;
  slide.drag(-60, 100);
  slide.presented(100);
  slide.release(true, 200);
  assert(slide.active());
  assert(!slide.dragging());
  assert(slide.due(200));
  assertFrame(slide.frame(200), -60, 180, false);
  assertFrame(slide.frame(275), -217, 23, false);
  assert(!slide.frame(349).finished);
  assertFrame(slide.frame(350), -240, 0, true);
}

void canceledDragSnapsBackToOriginalFace() {
  WatchfaceSlide slide;
  slide.drag(30, 100);
  slide.release(false, 200);
  assertFrame(slide.frame(200), 30, -210, false);
  const auto middle = slide.frame(230);
  assert(middle.outgoing_x > 0 && middle.outgoing_x < 30);
  assert(!middle.finished);
  assertFrame(slide.frame(260), 0, -240, true);
}

void longHeldDragCanStillSettle() {
  WatchfaceSlide slide;
  slide.drag(150, 100);
  slide.presented(100);
  assert(!slide.due(10000));
  slide.release(true, 10000);
  assertFrame(slide.frame(10000), 150, -90, false);
  assertFrame(slide.frame(10075), 240, 0, true);
}

void committedAndCanceledMotionStayAdjacentAndMonotonic() {
  for (int start : {-120, 120}) {
    for (bool commit : {false, true}) {
      WatchfaceSlide slide;
      slide.drag(start, 100);
      slide.release(commit, 200);
      int previous = start;
      for (uint32_t elapsed = 0; elapsed <= 200; ++elapsed) {
        const auto frame = slide.frame(200 + elapsed);
        assert(frame.incoming_x - frame.outgoing_x == (start < 0 ? 240 : -240));
        assert(frame.outgoing_x >= -240 && frame.outgoing_x <= 240);
        if ((start < 0) == commit) assert(frame.outgoing_x <= previous);
        else assert(frame.outgoing_x >= previous);
        previous = frame.outgoing_x;
      }
      assertFrame(slide.frame(400), commit ? (start < 0 ? -240 : 240) : 0,
                  commit ? 0 : (start < 0 ? 240 : -240), true);
    }
  }
}

void zeroDistanceReleaseFinishesImmediately() {
  WatchfaceSlide slide;
  slide.drag(0, 100);
  slide.release(false, 200);
  assert(slide.due(200));
  assertFrame(slide.frame(200), 0, 240, true);
  slide.cancel();
  slide.drag(-240, 300);
  slide.release(true, 400);
  assert(slide.due(400));
  assertFrame(slide.frame(400), -240, 0, true);
}

void cancelInterruptsDragAndSettling() {
  WatchfaceSlide slide;
  slide.drag(60, 100);
  slide.cancel();
  assert(!slide.active());
  assert(!slide.dragging());
  assert(!slide.due(200));
  slide.drag(-80, 300);
  slide.release(true, 400);
  slide.cancel();
  assert(!slide.active());
  assert(!slide.due(600));
  slide.drag(20, 700);
  assertFrame(slide.frame(700), 20, -220, false);
  assert(slide.due(700));
}

void dragAndSettleTimingSurvivesMillisRollover() {
  WatchfaceSlide slide;
  const uint32_t near_wrap = std::numeric_limits<uint32_t>::max() - 9U;
  slide.drag(20, near_wrap);
  slide.presented(near_wrap);
  slide.drag(30, 0);
  assert(!slide.due(5));
  assert(slide.due(6));
  slide.release(false, near_wrap);
  assertFrame(slide.frame(near_wrap), 30, -210, false);
  assert(!slide.frame(49).finished);
  assertFrame(slide.frame(50), 0, -240, true);
}

void releaseWithInvalidWidthOrWithoutDragIsSafe() {
  WatchfaceSlide slide;
  slide.release(true, 100);
  assert(!slide.active());
  slide.drag(20, 100, 0);
  assert(!slide.active());
  slide.drag(20, 100);
  slide.release(true, 200, -1);
  assert(!slide.active());
  slide.start(SwipeDirection::Right, 300);
  slide.release(false, 350);
  assertFrame(slide.frame(400), 210, -30, false);
}

}  // namespace

int main() {
  idleDoesNotScheduleOrRender();
  leftSlideStartsAndEndsAtExactPositions();
  rightSlideStartsAndEndsAtExactPositions();
  cubicEasingMovesQuicklyThenSettles();
  facesStayAdjacentAndMotionIsMonotonic();
  cadenceStartsWithAnImmediateFrame();
  completionDoesNotWaitForAnotherCadenceInterval();
  missedFramesDoNotLengthenAnimation();
  timingSurvivesMillisRollover();
  cancelAndRestartUseTheNewDirectionAndTime();
  noDirectionCancelsAnExistingSlide();
  customAndInvalidWidthsAreSafe();
  dragTracksTheFingerWithoutFinishing();
  stationaryFingerDoesNotRedraw();
  dragSamplesDoNotRestartFrameCadence();
  returningToPresentedOffsetNeedsNoFrame();
  reversalRetainsSideUntilItCrossesZero();
  dragOffsetsClampAtEitherEdge();
  releaseCommitsFromCurrentOffsetWithoutJumping();
  canceledDragSnapsBackToOriginalFace();
  longHeldDragCanStillSettle();
  committedAndCanceledMotionStayAdjacentAndMonotonic();
  zeroDistanceReleaseFinishesImmediately();
  cancelInterruptsDragAndSettling();
  dragAndSettleTimingSurvivesMillisRollover();
  releaseWithInvalidWidthOrWithoutDragIsSafe();
  std::puts("Watchface slide: all 26 scenarios passed.");
}
