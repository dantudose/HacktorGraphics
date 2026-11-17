#include "BleAdvertisingSchedule.hpp"

#include <cassert>
#include <cstdio>
#include <limits>

namespace {

void disabledHasNoDeadlineAndEnableStartsImmediately() {
  watch::BleAdvertisingSchedule schedule;
  assert(!schedule.shouldStart(100));
  assert(schedule.waitMs(100) == UINT32_MAX);
  assert(!schedule.keepRadio(100, true));
  schedule.beginWindow(100);
  assert(!schedule.keepRadio(101, true));
  schedule.setEnabled(true);
  assert(schedule.shouldStart(100) && schedule.waitMs(100) == 0);
  schedule.beginWindow(100);
  assert(!schedule.shouldStart(100));
  assert(schedule.keepRadio(100, false) && schedule.waitMs(100) == 3000);
  assert(schedule.keepRadio(3099, false) && schedule.waitMs(3099) == 1);
  assert(!schedule.keepRadio(3100, false));
  assert(schedule.waitMs(3100) == 2000);
}

void failedInitializationStillConsumesTheThreeSecondWindowAndFiveSecondCycle() {
  watch::BleAdvertisingSchedule schedule;
  schedule.setEnabled(true);
  schedule.beginWindow(1000); // Transport initialization then fails.
  assert(schedule.keepRadio(1000, false));
  assert(!schedule.shouldStart(1001));
  schedule.beginWindow(2000); // Retry cannot move the window's anchor.
  assert(schedule.waitMs(2000) == 2000);
  assert(!schedule.keepRadio(4000, false));
  assert(!schedule.shouldStart(5999) && schedule.waitMs(5999) == 1);
  assert(schedule.shouldStart(6000));
  schedule.beginWindow(6000);
  assert(schedule.keepRadio(6000, false));
  assert(schedule.waitMs(6000) == 3000);
}

void aConnectionHasThirtySecondsFromItsFirstObservation() {
  watch::BleAdvertisingSchedule schedule;
  schedule.setEnabled(true);
  schedule.beginWindow(100);
  assert(schedule.keepRadio(1000, true));
  assert(schedule.waitMs(1000) == 30000);
  assert(!schedule.shouldStart(6000)); // Do not overlap an attached phone.
  assert(schedule.keepRadio(30999, true) && schedule.waitMs(30999) == 1);
  assert(!schedule.keepRadio(31000, true));
  assert(schedule.waitMs(31000) == 0);
  // A phone that races shutdown cannot reopen the expired connection window.
  assert(!schedule.keepRadio(31001, true));
  assert(schedule.shouldStart(31001)); // A new cycle can start after shutdown.
}

void reconnectingDuringTheInitialBurstCannotExtendTheConnectionCap() {
  watch::BleAdvertisingSchedule schedule;
  schedule.setEnabled(true);
  schedule.beginWindow(0);
  assert(schedule.keepRadio(500, true));
  assert(schedule.keepRadio(1000, false) && schedule.waitMs(1000) == 2000);
  assert(schedule.keepRadio(2000, true));
  assert(schedule.waitMs(2000) == 28500);
  assert(!schedule.keepRadio(30500, true));
}

void disconnectingAfterTheBurstClosesImmediately() {
  watch::BleAdvertisingSchedule schedule;
  schedule.setEnabled(true);
  schedule.beginWindow(0);
  assert(schedule.keepRadio(2500, true));
  assert(schedule.keepRadio(4000, true));
  assert(!schedule.keepRadio(4100, false));
  assert(schedule.waitMs(4100) == 900);
  assert(!schedule.keepRadio(4200, true));
}

void aLateFirstConnectionCannotReviveAnExpiredBurst() {
  watch::BleAdvertisingSchedule schedule;
  schedule.setEnabled(true);
  schedule.beginWindow(0);
  assert(!schedule.keepRadio(3000, true));
  assert(!schedule.keepRadio(3001, true));
  assert(schedule.waitMs(3001) == 1999);
}

void togglingOffClosesTheWindowAndEnablingStartsAFreshCycle() {
  watch::BleAdvertisingSchedule schedule;
  schedule.setEnabled(true);
  schedule.beginWindow(0);
  assert(schedule.keepRadio(1000, true));
  schedule.setEnabled(false);
  assert(!schedule.keepRadio(1001, true));
  assert(!schedule.shouldStart(1001) && schedule.waitMs(1001) == UINT32_MAX);
  schedule.setEnabled(true);
  assert(schedule.shouldStart(1002));
  schedule.beginWindow(1002);
  assert(schedule.keepRadio(2000, true));
  schedule.setEnabled(true); // Repeating the desired preference is harmless.
  assert(schedule.waitMs(2000) == 30000);
  assert(!schedule.keepRadio(32000, true));
}

void delayedServiceSkipsCatchUpWindowsAndAnchorsTheActualAttempt() {
  watch::BleAdvertisingSchedule schedule;
  schedule.setEnabled(true);
  schedule.beginWindow(0);
  assert(!schedule.keepRadio(3000, false));
  assert(schedule.shouldStart(180000));
  schedule.beginWindow(180000);
  assert(!schedule.shouldStart(180001));
  assert(!schedule.keepRadio(183000, false));
  assert(!schedule.shouldStart(184999));
  assert(schedule.shouldStart(185000));
}

void windowConnectionAndIntervalDeadlinesSurviveMillisRollover() {
  watch::BleAdvertisingSchedule schedule;
  const uint32_t start = std::numeric_limits<uint32_t>::max() - 1499U;
  schedule.setEnabled(true);
  schedule.beginWindow(start);
  assert(schedule.keepRadio(start + 1000U, true));
  assert(!schedule.shouldStart(start + 5000U));
  assert(schedule.keepRadio(start + 30999U, true));
  assert(!schedule.keepRadio(start + 31000U, true));
  assert(schedule.shouldStart(start + 31000U));
  schedule.beginWindow(start + 32000U); // Previous radio shutdown has completed.
  assert(schedule.waitMs(start + 32001U) == 2999);
  assert(!schedule.keepRadio(start + 35000U, false));
  assert(schedule.waitMs(start + 35000U) == 2000);
  assert(!schedule.shouldStart(start + 36999U));
  assert(schedule.shouldStart(start + 37000U));
}

}  // namespace

int main() {
  disabledHasNoDeadlineAndEnableStartsImmediately();
  failedInitializationStillConsumesTheThreeSecondWindowAndFiveSecondCycle();
  aConnectionHasThirtySecondsFromItsFirstObservation();
  reconnectingDuringTheInitialBurstCannotExtendTheConnectionCap();
  disconnectingAfterTheBurstClosesImmediately();
  aLateFirstConnectionCannotReviveAnExpiredBurst();
  togglingOffClosesTheWindowAndEnablingStartsAFreshCycle();
  delayedServiceSkipsCatchUpWindowsAndAnchorsTheActualAttempt();
  windowConnectionAndIntervalDeadlinesSurviveMillisRollover();
  std::puts("BLE advertising schedule: all 9 scenarios passed.");
}
