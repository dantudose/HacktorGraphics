#include "PowerStats.hpp"

#include <cassert>
#include <cstdio>
#include <limits>

using watch::PowerStats;
using watch::WakeReason;

namespace {

void eventsBeforeBeginAreIgnored() {
  PowerStats stats;
  stats.screen(true, 100, WakeReason::Button);
  stats.slept(200);
  const auto value = stats.snapshot(1000);
  assert(value.uptime_us == 0 && value.screen_on_us == 0);
  assert(value.light_sleep_us == 0 && value.wake_count == 0);
}

void darkTimeOnlyAdvancesUptime() {
  PowerStats stats;
  stats.begin(1000);
  const auto value = stats.snapshot(5000);
  assert(value.uptime_us == 4000);
  assert(value.screen_on_us == 0 && value.light_sleep_us == 0);
  assert(value.wake_count == 0);
}

void activeScreenIntervalAppearsWithoutClosingIt() {
  PowerStats stats;
  stats.begin(1000);
  stats.screen(true, 1500, WakeReason::Boot);
  auto value = stats.snapshot(3500);
  assert(value.uptime_us == 2500 && value.screen_on_us == 2000);
  assert(value.wake_count == 1 && value.other_wakes == 1);
  value = stats.snapshot(4500);
  assert(value.screen_on_us == 3000);
  assert(value.wake_count == 1);
}

void brightnessChangesAndFadeDoNotCountNewWakes() {
  PowerStats stats;
  stats.begin(0);
  stats.screen(true, 100, WakeReason::Button);
  stats.screen(true, 200, WakeReason::Motion);
  stats.screen(true, 300, WakeReason::Other);
  stats.screen(false, 1100);
  stats.screen(false, 1200);
  const auto value = stats.snapshot(2000);
  assert(value.screen_on_us == 1000);
  assert(value.wake_count == 1 && value.button_wakes == 1);
  assert(value.motion_wakes == 0 && value.other_wakes == 0);
}

void screenCyclesAccumulateAndClassifyWakes() {
  PowerStats stats;
  stats.begin(0);
  stats.screen(true, 0, WakeReason::Boot);
  stats.screen(false, 100);
  stats.screen(true, 200, WakeReason::Button);
  stats.screen(false, 400);
  stats.screen(true, 500, WakeReason::Motion);
  stats.screen(false, 800);
  stats.screen(true, 900);
  const auto value = stats.snapshot(1300);
  assert(value.screen_on_us == 1000);
  assert(value.wake_count == 4);
  assert(value.button_wakes == 1 && value.motion_wakes == 1);
  assert(value.other_wakes == 2);
  assert(value.wake_count == value.button_wakes + value.motion_wakes + value.other_wakes);
}

void sleepIntervalsAccumulateWithoutChangingScreenStats() {
  PowerStats stats;
  stats.begin(1000);
  stats.screen(true, 1000, WakeReason::Boot);
  stats.screen(false, 3000);
  stats.slept(4000);
  stats.slept(5000);
  stats.slept(0);
  const auto value = stats.snapshot(13000);
  assert(value.uptime_us == 12000 && value.light_sleep_us == 9000);
  assert(value.screen_on_us == 2000 && value.wake_count == 1);
}

void microsecondTimeDoesNotWrapAtMillisRollover() {
  PowerStats stats;
  const uint64_t start = 123456789ULL;
  const uint64_t interval = 50ULL * 24 * 60 * 60 * 1000000;
  stats.begin(start);
  stats.screen(true, start, WakeReason::Boot);
  auto value = stats.snapshot(start + interval);
  assert(value.screen_on_us == interval && value.uptime_us == interval);
  stats.screen(false, start + interval);
  value = stats.snapshot(start + interval + 1000);
  assert(value.screen_on_us == interval);
}

void beginStartsAFreshSession() {
  PowerStats stats;
  stats.begin(0);
  stats.screen(true, 100, WakeReason::Button);
  stats.slept(200);
  stats.begin(1000);
  auto value = stats.snapshot(1500);
  assert(value.uptime_us == 500 && value.screen_on_us == 0);
  assert(value.light_sleep_us == 0 && value.wake_count == 0);
  stats.screen(true, 1600, WakeReason::Boot);
  value = stats.snapshot(1700);
  assert(value.screen_on_us == 100 && value.wake_count == 1);
}

void staleTimestampsCannotUnderflow() {
  PowerStats stats;
  stats.begin(1000);
  stats.screen(true, 1100);
  stats.screen(false, 1050);
  const auto value = stats.snapshot(900);
  assert(value.uptime_us == 0 && value.screen_on_us == 0);
  assert(value.wake_count == 1);
}

void inconsistentSleepReportsAreBoundedByUptime() {
  PowerStats stats;
  stats.begin(0);
  const uint64_t maximum = std::numeric_limits<uint64_t>::max();
  stats.slept(maximum - 10);
  stats.slept(20);
  const auto value = stats.snapshot(1000);
  assert(value.light_sleep_us == 1000);
}

}  // namespace

int main() {
  eventsBeforeBeginAreIgnored();
  darkTimeOnlyAdvancesUptime();
  activeScreenIntervalAppearsWithoutClosingIt();
  brightnessChangesAndFadeDoNotCountNewWakes();
  screenCyclesAccumulateAndClassifyWakes();
  sleepIntervalsAccumulateWithoutChangingScreenStats();
  microsecondTimeDoesNotWrapAtMillisRollover();
  beginStartsAFreshSession();
  staleTimestampsCannotUnderflow();
  inconsistentSleepReportsAreBoundedByUptime();
  std::puts("Power stats: all 10 scenarios passed.");
}
