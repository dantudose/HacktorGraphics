#include "WatchTiming.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace {

constexpr uint32_t kSyncMs = 3600000;
constexpr uint32_t kRetryMs = 300000;

void clockKeepsTimeAcrossBlockingWorkAndSleep() {
  watch::Clock clock;
  const uint64_t anchor_us = 123456789123ULL;
  const uint64_t noon_ms = 43200000ULL;
  clock.set(noon_ms, anchor_us);

  // A five-second scan must advance five seconds, not just its remainder.
  assert(clock.at(anchor_us) == noon_ms);
  assert(clock.at(anchor_us + 5000000ULL) == noon_ms + 5000ULL);

  // No loop calls occur during this simulated twelve-hour sleep.
  assert(clock.at(anchor_us + 43200000000ULL) == noon_ms + 43200000ULL);
}

void repeatedReadsKeepFractionalMilliseconds() {
  watch::Clock clock;
  clock.set(1000, 100000);

  assert(clock.at(100125) == 1000);
  assert(clock.at(100750) == 1000);
  assert(clock.at(100999) == 1000);
  assert(clock.at(101000) == 1001);
  assert(clock.at(101125) == 1001);
  assert(clock.at(101999) == 1001);
  assert(clock.at(102000) == 1002);
}

void synchronizationReplacesTheAnchorInBothDirections() {
  watch::Clock clock;
  clock.set(43200000, 1000000);
  assert(clock.at(6000000) == 43205000);

  // CTS advances the watch by fifty-five seconds after a blocking scan.
  clock.set(43260000, 6000000);
  assert(clock.at(6000000) == 43260000);
  assert(clock.at(7000000) == 43261000);

  // A backward correction also discards all elapsed time before the update.
  clock.set(43200000, 7000000);
  assert(clock.at(7000000) == 43200000);
  assert(clock.at(9000000) == 43202000);
}

void midnightRetainsTheDayOffset() {
  watch::Clock clock;
  clock.set(86399750, 1000000);

  assert(clock.at(1250000) == 86400000);
  assert(clock.at(3000000) == 86401750);
  assert(clock.at(86401000000ULL) == 172799750);
}

void elapsedDeadlinesHandleBoundariesAndRollover() {
  assert(watch::remainingMs(1000, 1000, 3000) == 3000);
  assert(watch::remainingMs(3999, 1000, 3000) == 1);
  assert(watch::remainingMs(4000, 1000, 3000) == 0);
  assert(watch::remainingMs(9000, 1000, 3000) == 0);

  const uint32_t before_wrap = std::numeric_limits<uint32_t>::max() - 499;
  assert(watch::remainingMs(500, before_wrap, 3000) == 2000);
  assert(watch::remainingMs(2499, before_wrap, 3000) == 1);
  assert(watch::remainingMs(2500, before_wrap, 3000) == 0);
}

void anUnsynchronizedWatchRetriesEveryFiveMinutes() {
  assert(watch::ctsWaitMs(0, false, 0, 0, kSyncMs, kRetryMs) == 300000);
  assert(watch::ctsWaitMs(299999, false, 0, 0, kSyncMs, kRetryMs) == 1);
  assert(watch::ctsWaitMs(300000, false, 0, 0, kSyncMs, kRetryMs) == 0);

  // The attempt at five minutes failed; the next one is still five minutes away.
  assert(watch::ctsWaitMs(305000, false, 0, 300000, kSyncMs, kRetryMs) == 295000);
  assert(watch::ctsWaitMs(600000, false, 0, 300000, kSyncMs, kRetryMs) == 0);
}

void aSynchronizedWatchWaitsAnHourThenBacksOffOnFailure() {
  const uint32_t last_sync = 5000;
  assert(watch::ctsWaitMs(5000, true, last_sync, 0, kSyncMs, kRetryMs) == kSyncMs);
  assert(watch::ctsWaitMs(3604999, true, last_sync, 0, kSyncMs, kRetryMs) == 1);
  assert(watch::ctsWaitMs(3605000, true, last_sync, 0, kSyncMs, kRetryMs) == 0);

  // The hourly attempt failed. Being overdue must not cause continuous scans.
  const uint32_t failed_attempt = 3605000;
  assert(watch::ctsWaitMs(3610000, true, last_sync, failed_attempt,
                          kSyncMs, kRetryMs) == 295000);
  assert(watch::ctsWaitMs(3904999, true, last_sync, failed_attempt,
                          kSyncMs, kRetryMs) == 1);
  assert(watch::ctsWaitMs(3905000, true, last_sync, failed_attempt,
                          kSyncMs, kRetryMs) == 0);
}

void synchronizationSchedulingSurvivesMillisRollover() {
  const uint32_t max_ms = std::numeric_limits<uint32_t>::max();
  const uint32_t last_sync = max_ms - 1799999;

  // Half an hour elapsed before rollover; the other half elapses afterwards.
  assert(watch::ctsWaitMs(0, true, last_sync, last_sync,
                          kSyncMs, kRetryMs) == 1800000);
  assert(watch::ctsWaitMs(1799999, true, last_sync, last_sync,
                          kSyncMs, kRetryMs) == 1);
  assert(watch::ctsWaitMs(1800000, true, last_sync, last_sync,
                          kSyncMs, kRetryMs) == 0);

  const uint32_t failed_attempt = max_ms - 149999;
  assert(watch::ctsWaitMs(0, false, 0, failed_attempt,
                          kSyncMs, kRetryMs) == 150000);
  assert(watch::ctsWaitMs(150000, false, 0, failed_attempt,
                          kSyncMs, kRetryMs) == 0);
}

}  // namespace

int main() {
  clockKeepsTimeAcrossBlockingWorkAndSleep();
  repeatedReadsKeepFractionalMilliseconds();
  synchronizationReplacesTheAnchorInBothDirections();
  midnightRetainsTheDayOffset();
  elapsedDeadlinesHandleBoundariesAndRollover();
  anUnsynchronizedWatchRetriesEveryFiveMinutes();
  aSynchronizedWatchWaitsAnHourThenBacksOffOnFailure();
  synchronizationSchedulingSurvivesMillisRollover();
  std::puts("Watch timing: all 8 scenarios passed.");
}
