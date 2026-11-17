#include "StepCheckpoint.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace {

constexpr uint32_t kIntervalMs = watch::StepCheckpoint::kIntervalMs;

void startupWaitsAFullInterval() {
  watch::StepCheckpoint checkpoint;
  const uint32_t boot_ms = 1723;
  checkpoint.begin(boot_ms);
  assert(kIntervalMs == 1800000);
  assert(!checkpoint.due(boot_ms));
  assert(checkpoint.waitMs(boot_ms) == kIntervalMs);
  assert(!checkpoint.due(boot_ms + kIntervalMs - 1));
  assert(checkpoint.waitMs(boot_ms + kIntervalMs - 1) == 1);
  assert(checkpoint.due(boot_ms + kIntervalMs));
  assert(checkpoint.waitMs(boot_ms + kIntervalMs) == 0);
}

void everyAttemptDefersTheNextCheckpointRegardlessOfResult() {
  watch::StepCheckpoint checkpoint;
  checkpoint.begin(0);
  uint32_t now_ms = kIntervalMs;
  // Success, failure, and an unchanged sample all finish one checkpoint.
  for (unsigned outcome = 0; outcome < 3; ++outcome) {
    assert(checkpoint.due(now_ms));
    checkpoint.attempted(now_ms);
    assert(!checkpoint.due(now_ms));
    assert(checkpoint.waitMs(now_ms) == kIntervalMs);
    assert(!checkpoint.due(now_ms + kIntervalMs - 1));
    now_ms += kIntervalMs;
  }
  assert(checkpoint.due(now_ms));
}

void delayedServiceDoesNotCatchUpMissedCheckpoints() {
  watch::StepCheckpoint checkpoint;
  checkpoint.begin(500);
  const uint32_t delayed_ms = 500 + kIntervalMs * 4 + 12345;
  assert(checkpoint.due(delayed_ms));
  checkpoint.attempted(delayed_ms);
  assert(checkpoint.waitMs(delayed_ms) == kIntervalMs);
  assert(!checkpoint.due(delayed_ms + 1));
  assert(checkpoint.waitMs(delayed_ms + 1) == kIntervalMs - 1);
  assert(checkpoint.due(delayed_ms + kIntervalMs));
}

void deadlineAndAttemptsSurviveMillisRollover() {
  watch::StepCheckpoint checkpoint;
  const uint32_t start_ms = std::numeric_limits<uint32_t>::max() - 999;
  checkpoint.begin(start_ms);
  assert(checkpoint.waitMs(0) == kIntervalMs - 1000);
  assert(!checkpoint.due(kIntervalMs - 1001));
  assert(checkpoint.waitMs(kIntervalMs - 1001) == 1);
  assert(checkpoint.due(kIntervalMs - 1000));
  checkpoint.attempted(kIntervalMs - 1000);
  assert(checkpoint.waitMs(kIntervalMs - 1000) == kIntervalMs);
  assert(checkpoint.due(kIntervalMs * 2 - 1000));
}

void restartingAlwaysBeginsANewCooldown() {
  watch::StepCheckpoint checkpoint;
  checkpoint.begin(0);
  checkpoint.attempted(kIntervalMs);
  // Another processor boot starts millis near zero again. It must not save
  // immediately, even if the previous process had just passed its deadline.
  checkpoint.begin(20);
  assert(checkpoint.waitMs(20) == kIntervalMs);
  assert(!checkpoint.due(kIntervalMs + 19));
  assert(checkpoint.due(kIntervalMs + 20));
}

}  // namespace

int main() {
  startupWaitsAFullInterval();
  everyAttemptDefersTheNextCheckpointRegardlessOfResult();
  delayedServiceDoesNotCatchUpMissedCheckpoints();
  deadlineAndAttemptsSurviveMillisRollover();
  restartingAlwaysBeginsANewCooldown();
  std::puts("Step checkpoint: all 5 scenarios passed.");
}
