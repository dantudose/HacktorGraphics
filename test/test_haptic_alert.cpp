#include "HapticAlert.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace {

constexpr uint32_t kMaxPlayMs = watch::HapticAlert::MAX_PLAY_MS;

void idleHasNoPollTimeoutOrDeadline() {
  watch::HapticAlert alert;
  assert(!alert.active() && !alert.playing() && !alert.settling());
  assert(!alert.pollDue(100000));
  assert(!alert.timedOut(100000));
  assert(!alert.stopDue(100000));
  assert(!alert.settle(100000));
  assert(!alert.update(100000));
  assert(alert.waitMs(100000) == std::numeric_limits<uint32_t>::max());
}

void playingPollsAtMostOncePerInterval() {
  watch::HapticAlert alert;
  assert(alert.start(1000));
  assert(alert.active() && alert.playing() && !alert.settling());
  assert(alert.waitMs(1000) == 25);
  assert(!alert.pollDue(1024));
  assert(alert.waitMs(1024) == 1);
  assert(alert.pollDue(1025));
  assert(alert.waitMs(1025) == 0);
  alert.markPolled(1025);
  assert(!alert.pollDue(1025));
  assert(alert.waitMs(1025) == 25);
  assert(!alert.pollDue(1049));
  assert(alert.pollDue(1050));
}

void aLatePollRebasesPollingWithoutAReplayBurst() {
  watch::HapticAlert alert;
  assert(alert.start(0));
  assert(alert.pollDue(500));
  alert.markPolled(500);
  assert(!alert.pollDue(500));
  assert(!alert.pollDue(524));
  assert(alert.pollDue(525));
  assert(alert.waitMs(500) == 25);
}

void activeStartCannotRestartTheTimeout() {
  watch::HapticAlert alert;
  assert(alert.start(1000));
  assert(!alert.start(2000));
  assert(!alert.timedOut(1000 + kMaxPlayMs - 1));
  assert(alert.timedOut(1000 + kMaxPlayMs));
  assert(!alert.pollDue(1000 + kMaxPlayMs));
  assert(alert.waitMs(1000 + kMaxPlayMs) == 0);
}

void statusPollingCannotExtendTheTimeout() {
  watch::HapticAlert alert;
  assert(alert.start(0));
  for (uint32_t now = 25; now < kMaxPlayMs; now += 25) {
    assert(alert.pollDue(now));
    alert.markPolled(now);
    assert(!alert.pollDue(now));
    assert(alert.waitMs(now) == 25);
    assert(!alert.timedOut(now));
  }
  assert(alert.waitMs(kMaxPlayMs - 1) == 1);
  assert(alert.timedOut(kMaxPlayMs));
  assert(!alert.pollDue(kMaxPlayMs));
  assert(alert.waitMs(kMaxPlayMs) == 0);
  assert(!alert.update(kMaxPlayMs)); // Main must stop the hardware, then settle.
}

void settlementFinishesOnceAfterItsQuietWindow() {
  watch::HapticAlert alert;
  assert(alert.start(1000));
  assert(alert.settle(1600));
  assert(alert.active() && !alert.playing() && alert.settling());
  assert(!alert.pollDue(1600));
  assert(!alert.timedOut(100000));
  assert(alert.waitMs(1600) == 200);
  assert(!alert.update(1799));
  assert(alert.waitMs(1799) == 1);
  assert(alert.waitMs(1800) == 0);
  assert(alert.update(1800));
  assert(!alert.active() && !alert.playing() && !alert.settling());
  assert(!alert.update(1800));
  assert(!alert.update(100000));
  assert(alert.waitMs(1800) == std::numeric_limits<uint32_t>::max());
}

void repeatedSettleCallsCannotPostponeCleanup() {
  watch::HapticAlert alert;
  assert(alert.start(0));
  assert(alert.settle(600));
  assert(!alert.settle(700));
  assert(!alert.start(750));
  alert.markPolled(775);
  assert(alert.waitMs(799) == 1);
  assert(alert.update(800));
}

void completionAllowsOneFreshAlert() {
  watch::HapticAlert alert;
  assert(alert.start(0));
  assert(alert.settle(600));
  assert(alert.update(800));
  assert(alert.start(1000));
  assert(alert.playing());
  assert(alert.waitMs(1000) == 25);
  assert(!alert.timedOut(1000 + kMaxPlayMs - 1));
  assert(alert.timedOut(1000 + kMaxPlayMs));
  assert(alert.settle(1000 + kMaxPlayMs));
  assert(alert.update(1000 + kMaxPlayMs + 200));
  assert(!alert.update(1000 + kMaxPlayMs + 201));
}

void lateTimeoutCleanupRemainsBoundedAfterTheStopCommand() {
  watch::HapticAlert alert;
  assert(alert.start(0));
  assert(alert.timedOut(5000));
  assert(!alert.pollDue(5000));
  assert(alert.waitMs(5000) == 0);
  assert(alert.settle(5000));
  assert(!alert.update(5199));
  assert(alert.update(5200));
  assert(!alert.active());
}

void pollingTimeoutAndSettlementSurviveMillisRollover() {
  watch::HapticAlert alert;
  const uint32_t start = std::numeric_limits<uint32_t>::max() - 9;
  assert(alert.start(start));
  assert(alert.waitMs(14) == 1);
  assert(!alert.pollDue(14));
  assert(alert.pollDue(15));
  alert.markPolled(15);
  assert(alert.waitMs(39) == 1);
  assert(alert.pollDue(40));
  assert(!alert.timedOut(kMaxPlayMs - 11));
  assert(alert.timedOut(kMaxPlayMs - 10));

  watch::HapticAlert settling;
  assert(settling.start(std::numeric_limits<uint32_t>::max() - 199));
  assert(settling.settle(std::numeric_limits<uint32_t>::max() - 99));
  assert(settling.waitMs(99) == 1);
  assert(!settling.update(99));
  assert(settling.update(100));
  assert(!settling.active());
}

void halfSecondConfirmationHasANormalStopAndFullQuietWindow() {
  watch::HapticAlert alert;
  assert(alert.start(1000, 500));
  assert(!alert.stopDue(1499));
  assert(alert.stopDue(1500));
  assert(!alert.timedOut(1500));
  assert(alert.settle(1500));
  assert(!alert.stopDue(1500));
  assert(!alert.update(1699));
  assert(alert.update(1700));
}

void confirmationDeadlineWinsOverARecentStatusPoll() {
  watch::HapticAlert alert;
  assert(alert.start(1000, 500));
  alert.markPolled(1499);
  assert(!alert.pollDue(1499));
  assert(!alert.stopDue(1499));
  assert(alert.waitMs(1499) == 1);
  assert(!alert.pollDue(1500));
  assert(alert.stopDue(1500));
  assert(alert.waitMs(1500) == 0);
}

void failedConfirmationStopsRetryWithoutBusyLoopOrEarlyGuardRelease() {
  watch::HapticAlert alert;
  assert(alert.start(0, 500));
  alert.markPolled(499);
  assert(alert.stopDue(500));
  alert.markPolled(500); // Main attempted to stop; I2C did not confirm it.
  assert(!alert.stopDue(500));
  assert(alert.waitMs(500) == 25);
  assert(!alert.stopDue(524));
  assert(alert.waitMs(524) == 1);
  assert(alert.stopDue(525));
  alert.markPolled(525);
  assert(!alert.stopDue(525));
  assert(alert.waitMs(525) == 25);
  assert(alert.active() && alert.playing());
  assert(!alert.timedOut(2999));
  assert(alert.timedOut(3000));
  assert(!alert.stopDue(3000));
  assert(alert.waitMs(3000) == 0);
  assert(alert.settle(3000));
  assert(!alert.update(3199));
  assert(alert.update(3200));
}

void aFreshAlertDoesNotInheritThePreviousNormalStop() {
  watch::HapticAlert alert;
  assert(alert.start(0, 500));
  assert(!alert.start(100, 1)); // An active alert cannot change its duration.
  assert(!alert.stopDue(499));
  assert(alert.stopDue(500));
  assert(alert.settle(500) && alert.update(700));
  assert(alert.start(1000)); // Timer uses the full watchdog without a soft stop.
  assert(!alert.stopDue(1500));
  assert(!alert.stopDue(3999));
  assert(!alert.timedOut(3999));
  assert(alert.timedOut(4000));
  assert(alert.settle(4000) && alert.update(4200));
  assert(alert.start(5000, 100));
  assert(!alert.stopDue(5099));
  assert(alert.stopDue(5100));
}

void anOversizeNormalDurationCannotExtendTheHardWatchdog() {
  watch::HapticAlert alert;
  assert(alert.start(0, std::numeric_limits<uint32_t>::max()));
  alert.markPolled(2999);
  assert(!alert.stopDue(2999));
  assert(!alert.timedOut(2999));
  assert(alert.waitMs(2999) == 1);
  assert(alert.timedOut(3000));
  assert(!alert.stopDue(3000));
  assert(alert.waitMs(3000) == 0);
}

void confirmationStopAndRetrySurviveMillisRollover() {
  watch::HapticAlert alert;
  const uint32_t start = std::numeric_limits<uint32_t>::max() - 249;
  assert(alert.start(start, 500));
  alert.markPolled(249); // 499 ms after start.
  assert(!alert.stopDue(249));
  assert(alert.waitMs(249) == 1);
  assert(alert.stopDue(250));
  alert.markPolled(250);
  assert(!alert.stopDue(274));
  assert(alert.waitMs(274) == 1);
  assert(alert.stopDue(275));
  assert(!alert.timedOut(2749));
  assert(alert.timedOut(2750));
  assert(alert.settle(2750));
  assert(!alert.update(2949));
  assert(alert.update(2950));
}

}  // namespace

int main() {
  idleHasNoPollTimeoutOrDeadline();
  playingPollsAtMostOncePerInterval();
  aLatePollRebasesPollingWithoutAReplayBurst();
  activeStartCannotRestartTheTimeout();
  statusPollingCannotExtendTheTimeout();
  settlementFinishesOnceAfterItsQuietWindow();
  repeatedSettleCallsCannotPostponeCleanup();
  completionAllowsOneFreshAlert();
  lateTimeoutCleanupRemainsBoundedAfterTheStopCommand();
  pollingTimeoutAndSettlementSurviveMillisRollover();
  halfSecondConfirmationHasANormalStopAndFullQuietWindow();
  confirmationDeadlineWinsOverARecentStatusPoll();
  failedConfirmationStopsRetryWithoutBusyLoopOrEarlyGuardRelease();
  aFreshAlertDoesNotInheritThePreviousNormalStop();
  anOversizeNormalDurationCannotExtendTheHardWatchdog();
  confirmationStopAndRetrySurviveMillisRollover();
  std::puts("Haptic alert: all 16 scenarios passed.");
}
