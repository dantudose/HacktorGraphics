#include "WatchButton.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace {

void expect(watch::ButtonEvents events, bool pressed = false,
            bool released = false, bool short_press = false,
            bool long_press = false) {
  assert(events.pressed == pressed);
  assert(events.released == released);
  assert(events.short_press == short_press);
  assert(events.long_press == long_press);
}

void aTapWakesAfterDebounceAndClicksOnRelease() {
  watch::WatchButton button;
  button.reset(false, 0);
  assert(button.isSettled());
  assert(!button.shouldStayAwake());
  expect(button.update(true, 100));
  assert(!button.isPressed());
  assert(!button.isSettled());
  assert(button.shouldStayAwake());
  expect(button.update(true, 129));
  expect(button.update(true, 130), true);
  assert(button.isPressed());
  assert(button.isSettled());
  expect(button.update(false, 300));
  assert(button.shouldStayAwake());
  assert(!button.isSettled());
  expect(button.update(false, 329));
  expect(button.update(false, 330), false, true, true);
  assert(!button.isPressed());
  assert(button.isSettled());
  assert(!button.shouldStayAwake());
  expect(button.update(false, 1000));
}

void bounceAtBothEdgesProducesOneGesture() {
  watch::WatchButton button;
  button.reset(false, 0);
  expect(button.update(true, 100));
  expect(button.update(false, 105));
  expect(button.update(true, 112));
  expect(button.update(false, 120));
  expect(button.update(true, 125));
  expect(button.update(true, 154));
  expect(button.update(true, 155), true);

  expect(button.update(false, 300));
  expect(button.update(true, 308));
  expect(button.update(false, 315));
  expect(button.update(true, 322));
  expect(button.update(false, 328));
  expect(button.update(false, 357));
  expect(button.update(false, 358), false, true, true);
  expect(button.update(false, 500));
}

void aRejectedPressBounceDoesNotBecomeAGesture() {
  watch::WatchButton button;
  button.reset(false, 0);
  expect(button.update(true, 100));
  expect(button.update(false, 110));
  assert(!button.isSettled());
  assert(button.shouldStayAwake());
  expect(button.update(false, 139));
  expect(button.update(false, 140));
  assert(button.isSettled());
  assert(!button.shouldStayAwake());

  // The abandoned bounce cannot lengthen a later press into a long press.
  expect(button.update(true, 1000));
  expect(button.update(true, 1030), true);
  expect(button.update(false, 1799));
  expect(button.update(false, 1829), false, true, true);
}

void longPressFiresExactlyOnceAtTheThreshold() {
  watch::WatchButton button;
  button.reset(false, 0);
  expect(button.update(true, 100));
  expect(button.update(true, 130), true);
  expect(button.update(true, 899));
  expect(button.update(true, 900), false, false, false, true);
  expect(button.update(true, 901));
  expect(button.update(true, 20000));
  assert(button.shouldStayAwake());
  expect(button.update(false, 20100));
  expect(button.update(false, 20130), false, true);
  expect(button.update(false, 21000));
  assert(!button.shouldStayAwake());
}

void pressBouncePreservesTheFirstPressEdge() {
  watch::WatchButton button;
  button.reset(false, 0);
  expect(button.update(true, 100));
  expect(button.update(false, 105));
  expect(button.update(true, 112));
  expect(button.update(true, 142), true);
  expect(button.update(true, 899));
  expect(button.update(true, 900), false, false, false, true);
  expect(button.update(false, 950));
  expect(button.update(false, 980), false, true);
}

void releaseDebounceDoesNotTurnAShortPressIntoALongPress() {
  watch::WatchButton button;
  button.reset(false, 0);
  expect(button.update(true, 100));
  expect(button.update(true, 130), true);
  expect(button.update(false, 899));
  expect(button.update(false, 900));
  expect(button.update(false, 928));
  expect(button.update(false, 929), false, true, true);

  expect(button.update(true, 2000));
  expect(button.update(true, 2030), true);
  expect(button.update(false, 2800), false, false, false, true);
  expect(button.update(false, 2830), false, true);
}

void releaseAfterBlockingWorkClassifiesTheActualHold() {
  watch::WatchButton button;
  button.reset(false, 0);
  expect(button.update(true, 100));
  expect(button.update(true, 130), true);
  // No poll occurs during display initialization, then the user releases.
  expect(button.update(false, 350));
  expect(button.update(false, 380), false, true, true);

  expect(button.update(true, 1000));
  expect(button.update(true, 1030), true);
  // Missing the 800 ms poll still produces a long action, never a short one.
  expect(button.update(false, 2000), false, false, false, true);
  expect(button.update(false, 2030), false, true);
}

void pollingCanResumeOnTheReleaseEdge() {
  watch::WatchButton button;
  button.reset(false, 0);
  expect(button.update(true, 100));
  // Even the original press debounce elapsed during blocking work.
  expect(button.update(false, 300), true);
  expect(button.update(false, 330), false, true, true);

  expect(button.update(true, 1000));
  expect(button.update(false, 2000), true, false, false, true);
  expect(button.update(false, 2030), false, true);
}

void startupHeldIsIgnoredUntilASettledRelease() {
  watch::WatchButton button;
  button.reset(true, 100);
  assert(button.isPressed());
  assert(button.isSettled());
  assert(button.shouldStayAwake());
  expect(button.update(true, 10000));
  expect(button.update(false, 10100));
  expect(button.update(true, 10110));
  expect(button.update(false, 10120));
  expect(button.update(false, 10150), false, true);
  assert(!button.shouldStayAwake());
  expect(button.update(true, 11000));
  expect(button.update(true, 11030), true);
  expect(button.update(false, 11100));
  expect(button.update(false, 11130), false, true, true);
}

void resettingAHeldButtonCancelsItsGesture() {
  watch::WatchButton button;
  button.reset(false, 0);
  expect(button.update(true, 100));
  expect(button.update(true, 130), true);
  button.reset(true, 200);
  expect(button.update(true, 2000));
  expect(button.update(false, 2100));
  expect(button.update(false, 2130), false, true);
  assert(!button.shouldStayAwake());
}

void repeatedGesturesResetTheHoldState() {
  watch::WatchButton button;
  button.reset(false, 0);
  for (uint32_t start = 100; start <= 3100; start += 1000) {
    expect(button.update(true, start));
    expect(button.update(true, start + 30), true);
    const bool long_press = start == 1100 || start == 3100;
    if (long_press) {
      expect(button.update(true, start + 800), false, false, false, true);
    }
    const uint32_t release = start + (long_press ? 850 : 100);
    expect(button.update(false, release));
    expect(button.update(false, release + 30), false, true, !long_press);
    assert(!button.shouldStayAwake());
  }
}

void debounceAndGesturesSurviveMillisRollover() {
  watch::WatchButton button;
  const uint32_t max_ms = std::numeric_limits<uint32_t>::max();
  button.reset(false, max_ms - 100);
  expect(button.update(true, max_ms - 20));
  expect(button.update(true, 8));
  expect(button.update(true, 9), true);
  expect(button.update(true, 778));
  expect(button.update(true, 779), false, false, false, true);
  expect(button.update(false, 800));
  expect(button.update(false, 830), false, true);

  button.reset(false, max_ms - 100);
  expect(button.update(true, max_ms - 80));
  expect(button.update(true, max_ms - 50), true);
  expect(button.update(false, max_ms - 10));
  expect(button.update(false, 18));
  expect(button.update(false, 19), false, true, true);
}

}  // namespace

int main() {
  aTapWakesAfterDebounceAndClicksOnRelease();
  bounceAtBothEdgesProducesOneGesture();
  aRejectedPressBounceDoesNotBecomeAGesture();
  longPressFiresExactlyOnceAtTheThreshold();
  pressBouncePreservesTheFirstPressEdge();
  releaseDebounceDoesNotTurnAShortPressIntoALongPress();
  releaseAfterBlockingWorkClassifiesTheActualHold();
  pollingCanResumeOnTheReleaseEdge();
  startupHeldIsIgnoredUntilASettledRelease();
  resettingAHeldButtonCancelsItsGesture();
  repeatedGesturesResetTheHoldState();
  debounceAndGesturesSurviveMillisRollover();
  std::puts("Watch button: all 12 scenarios passed.");
}
