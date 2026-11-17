#include "WatchTimer.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace {

void defaultTimerIsReadyForFiveMinutes() {
  const watch::WatchTimer timer;
  assert(timer.state() == watch::TimerState::Ready);
  assert(timer.durationSeconds() == 300);
  assert(timer.remainingMs(0) == 300000);
  assert(timer.remainingMs(900000000ULL) == 300000);
  assert(timer.waitUs(0) == std::numeric_limits<uint64_t>::max());
}

void durationSelectionIncludesZeroAndClampsTheMaximum() {
  watch::WatchTimer timer;
  assert(timer.setDurationSeconds(0));
  assert(timer.remainingMs(0) == 0);
  assert(!timer.start(1000));
  assert(timer.state() == watch::TimerState::Ready);
  assert(!timer.update(1000000));
  assert(timer.setDurationSeconds(std::numeric_limits<uint32_t>::max()));
  assert(timer.durationSeconds() == 5999);
  assert(timer.remainingMs(0) == 5999000);
  const watch::WatchTimer constructor_clamps(6000);
  assert(constructor_clamps.durationSeconds() == 5999);
}

void selectedFieldsClampWithoutCarrying() {
  watch::WatchTimer timer(5U * 60U + 30U);
  assert(timer.adjustMinutes(1));
  assert(timer.durationSeconds() == 6U * 60U + 30U);
  assert(timer.adjustSeconds(1));
  assert(timer.durationSeconds() == 6U * 60U + 31U);
  assert(timer.adjustMinutes(-1000));
  assert(timer.durationSeconds() == 31);
  assert(timer.adjustSeconds(1000));
  assert(timer.durationSeconds() == 59);
  assert(timer.adjustSeconds(1));
  assert(timer.durationSeconds() == 59);
  assert(timer.adjustMinutes(std::numeric_limits<int>::max()));
  assert(timer.durationSeconds() == 5999);
  assert(timer.adjustSeconds(std::numeric_limits<int>::min()));
  assert(timer.durationSeconds() == 99U * 60U);
  assert(timer.adjustMinutes(std::numeric_limits<int>::min()));
  assert(timer.durationSeconds() == 0);
}

void runningTimerExpiresOnlyAtItsDeadline() {
  watch::WatchTimer timer(2);
  assert(timer.start(123456));
  assert(timer.state() == watch::TimerState::Running);
  assert(timer.remainingMs(123456) == 2000);
  assert(timer.remainingMs(123457) == 2000);
  assert(timer.remainingMs(124456) == 1999);
  assert(timer.remainingMs(2123455) == 1);
  assert(timer.waitUs(2123455) == 1);
  assert(!timer.update(2123455));
  assert(timer.remainingMs(2123456) == 0);
  assert(timer.waitUs(2123456) == 0);
  assert(timer.update(2123456));
  assert(timer.state() == watch::TimerState::Ready);
  assert(timer.durationSeconds() == 2);
  assert(timer.remainingMs(3000000) == 2000);
  assert(timer.waitUs(3000000) == std::numeric_limits<uint64_t>::max());
  assert(!timer.update(3000000));
  assert(!timer.update(5000000000ULL));
  assert(timer.remainingMs(5000000000ULL) == 2000);
}

void pauseAndResumeKeepFractionalMilliseconds() {
  watch::WatchTimer timer(1);
  assert(timer.start(1000));
  assert(timer.pause(101001));
  assert(timer.state() == watch::TimerState::Paused);
  assert(timer.remainingMs(101001) == 900);
  assert(timer.remainingMs(5000000000ULL) == 900);
  assert(timer.waitUs(5000000000ULL) == std::numeric_limits<uint64_t>::max());
  assert(!timer.update(5000000000ULL));
  assert(timer.start(5000000000ULL));
  assert(timer.waitUs(5000000000ULL) == 899999);
  assert(!timer.update(5000899998ULL));
  assert(timer.update(5000899999ULL));
}

void repeatedPausesDoNotLoseTime() {
  watch::WatchTimer timer(1);
  uint64_t start = 1000000;
  for (unsigned i = 0; i < 9; ++i) {
    assert(timer.start(start));
    assert(timer.pause(start + 100001));
    assert(timer.remainingMs(start + 100001) == 900 - i * 100);
    start += 1000000;
  }
  assert(timer.start(start));
  assert(timer.waitUs(start) == 99991);
  assert(!timer.update(start + 99990));
  assert(timer.update(start + 99991));
}

void editingCannotChangeActiveOrPausedTimers() {
  watch::WatchTimer timer(61);
  assert(!timer.pause(0));
  assert(timer.start(1000));
  assert(!timer.start(2000));
  assert(!timer.setDurationSeconds(5));
  assert(!timer.adjustMinutes(1));
  assert(!timer.adjustSeconds(-1));
  assert(timer.pause(2000));
  assert(!timer.pause(3000));
  assert(!timer.setDurationSeconds(5));
  assert(!timer.adjustMinutes(1));
  assert(!timer.adjustSeconds(-1));
  assert(timer.start(4000));
  assert(timer.update(70000000));
  assert(timer.state() == watch::TimerState::Ready);
  assert(timer.durationSeconds() == 61);
  assert(timer.remainingMs(70000000) == 61000);
  assert(timer.setDurationSeconds(5));
  assert(timer.adjustMinutes(1));
  assert(timer.adjustSeconds(-1));
  assert(timer.durationSeconds() == 64);
}

void elapsedPauseDoesNotSwallowTheCompletionEvent() {
  watch::WatchTimer timer(1);
  assert(timer.start(100));
  assert(!timer.pause(1000100));
  assert(timer.state() == watch::TimerState::Running);
  assert(timer.waitUs(1000100) == 0);
  assert(timer.update(1000100));
  assert(!timer.pause(1000200));
  assert(!timer.update(1000200));
}

void resetCancelsRunningPausedAndReadyTimers() {
  watch::WatchTimer timer(2);
  assert(timer.start(0));
  timer.reset();
  assert(timer.state() == watch::TimerState::Ready);
  assert(timer.remainingMs(1000000) == 2000);
  assert(!timer.update(5000000));
  assert(timer.start(5000000));
  assert(timer.pause(6000000));
  timer.reset();
  assert(timer.remainingMs(7000000) == 2000);
  assert(timer.start(7000000));
  assert(timer.update(9000000));
  timer.reset();
  assert(timer.state() == watch::TimerState::Ready);
  assert(timer.remainingMs(10000000) == 2000);
  assert(timer.start(10000000));
  assert(timer.update(12000000));
  assert(!timer.update(12000001));
  timer.reset();
  assert(timer.setDurationSeconds(3));
  assert(timer.remainingMs(12000001) == 3000);
}

void aSleepPastTheDeadlineEmitsOneCompletion() {
  watch::WatchTimer timer(5999);
  assert(timer.start(600000000000ULL));
  assert(timer.waitUs(600000000000ULL) == 5999000000ULL);
  assert(timer.update(643200000000ULL)); // Twelve hours later.
  assert(timer.state() == watch::TimerState::Ready);
  assert(timer.durationSeconds() == 5999);
  assert(timer.remainingMs(643200000000ULL) == 5999000);
  assert(timer.waitUs(643200000000ULL) == std::numeric_limits<uint64_t>::max());
  assert(!timer.update(643200000001ULL));
  assert(!timer.update(686400000000ULL)); // Another twelve hours must not repeat.
}

void eachExplicitRestartProducesOneCompletionAtTheSameDuration() {
  watch::WatchTimer timer(83);
  uint64_t now = 1000000;
  for (unsigned cycle = 0; cycle < 4; ++cycle) {
    assert(timer.state() == watch::TimerState::Ready);
    assert(timer.durationSeconds() == 83);
    assert(timer.remainingMs(now) == 83000);
    assert(timer.waitUs(now) == std::numeric_limits<uint64_t>::max());
    assert(timer.start(now));
    now += 83000000;
    assert(!timer.update(now - 1));
    assert(timer.update(now));
    assert(!timer.update(now));
    assert(!timer.update(now + 1000000000));
    now += 2000000000;
  }
}

void autoResetMakesTheConfiguredTimerEditableAgain() {
  watch::WatchTimer timer(1);
  assert(timer.start(5000));
  assert(timer.update(1005000));
  assert(timer.state() == watch::TimerState::Ready);
  assert(timer.adjustMinutes(2));
  assert(timer.adjustSeconds(8));
  assert(timer.durationSeconds() == 129);
  assert(timer.remainingMs(1005000) == 129000);
  assert(!timer.update(1000000000));
  assert(timer.start(1000000000));
  assert(!timer.update(1128999999));
  assert(timer.update(1129000000));
  assert(timer.remainingMs(1129000000) == 129000);
}

void uptimeBeyondMillisRolloverDoesNotAffectCountdown() {
  const uint64_t start =
      (static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) - 100ULL) * 1000ULL;
  watch::WatchTimer timer(2);
  assert(timer.start(start));
  assert(timer.remainingMs(start + 101000) == 1899);
  assert(!timer.update(start + 1999999));
  assert(timer.update(start + 2000000));
}

void monotonicTimestampRolloverKeepsElapsedTime() {
  const uint64_t start = std::numeric_limits<uint64_t>::max() - 999ULL;
  watch::WatchTimer timer(1);
  assert(timer.start(start));
  assert(timer.remainingMs(0) == 999);
  assert(timer.waitUs(0) == 999000);
  assert(!timer.update(998999));
  assert(timer.update(999000));
}

}  // namespace

int main() {
  defaultTimerIsReadyForFiveMinutes();
  durationSelectionIncludesZeroAndClampsTheMaximum();
  selectedFieldsClampWithoutCarrying();
  runningTimerExpiresOnlyAtItsDeadline();
  pauseAndResumeKeepFractionalMilliseconds();
  repeatedPausesDoNotLoseTime();
  editingCannotChangeActiveOrPausedTimers();
  elapsedPauseDoesNotSwallowTheCompletionEvent();
  resetCancelsRunningPausedAndReadyTimers();
  aSleepPastTheDeadlineEmitsOneCompletion();
  eachExplicitRestartProducesOneCompletionAtTheSameDuration();
  autoResetMakesTheConfiguredTimerEditableAgain();
  uptimeBeyondMillisRolloverDoesNotAffectCountdown();
  monotonicTimestampRolloverKeepsElapsedTime();
  std::puts("Watch timer: all 14 scenarios passed.");
}
