#include "StepCheckpoint.hpp"
#include "StepTracking.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

struct TestStore {
  std::vector<uint8_t> blob;
  size_t writes = 0;
  bool fail_write = false;

  size_t getBytesLength(const char* key) const {
    assert(std::strcmp(key, watch::stepHistoryKey()) == 0);
    return blob.size();
  }

  size_t getBytes(const char* key, void* destination, size_t size) const {
    assert(std::strcmp(key, watch::stepHistoryKey()) == 0);
    if (blob.empty() || blob.size() != size) return 0;
    std::memcpy(destination, blob.data(), size);
    return size;
  }

  size_t putBytes(const char* key, const void* source, size_t size) {
    assert(std::strcmp(key, watch::stepHistoryKey()) == 0);
    ++writes;
    if (fail_write) return 0;
    const auto* bytes = static_cast<const uint8_t*>(source);
    blob.assign(bytes, bytes + size);
    return size;
  }
};

watch::StepHistorySnapshot reload(TestStore& store) {
  watch::StepHistorySnapshot snapshot;
  assert(watch::loadStepHistory(store, snapshot));
  return snapshot;
}

bool checkpointIfDue(watch::StepTracking& steps,
                     watch::StepCheckpoint& checkpoint, TestStore& store,
                     uint32_t now_ms, int32_t day, uint16_t raw,
                     uint32_t second_of_day) {
  if (!checkpoint.due(now_ms)) return false;
  steps.sample(day, raw);
  checkpoint.attempted(now_ms);
  return steps.save(store, second_of_day);
}

void sameDayResetRestoresTheTotalAndTakesAFreshBaseline() {
  TestStore store;
  watch::StepTracking steps;
  steps.sample(700, 1000);
  steps.sample(700, 1234);
  assert(steps.count() == 234 && steps.dirty());
  assert(steps.save(store, 43200));
  assert(!steps.dirty() && store.writes == 1);

  const auto snapshot = reload(store);
  assert(snapshot.checkpoint_day == 700 && snapshot.checkpoint_second == 43200);
  watch::StepTracking restarted;
  restarted.restore(snapshot.history);
  assert(!restarted.hasDay() && !restarted.dirty());
  assert(restarted.observeDay(snapshot.checkpoint_day));
  assert(restarted.count() == 234 && restarted.needsBaseline());
  // The IMU reset independently; comparing 5 with the old 1234 would look
  // like a hardware wrap and invent more than sixty thousand steps.
  restarted.sample(700, 5);
  assert(restarted.count() == 234 && !restarted.needsBaseline());
  assert(!restarted.dirty());
  restarted.sample(700, 14);
  assert(restarted.count() == 243 && restarted.dirty());
  assert(restarted.save(store, 43260));
  assert(reload(store).history.count(700) == 243 && store.writes == 2);
}

void unchangedTotalsDoNotWriteEvenWhenTheTimestampAdvances() {
  TestStore store;
  watch::StepTracking steps;
  steps.sample(100, 10);
  steps.sample(100, 30);
  assert(steps.save(store, 1000));
  for (uint32_t second = 2000; second <= 8000; second += 1000) {
    steps.sample(100, 30);
    assert(!steps.dirty());
    assert(steps.save(store, second));
  }
  assert(store.writes == 1);
  assert(reload(store).checkpoint_second == 1000);
}

void midnightClosesTheOldDayWithTheBoundaryRawSample() {
  TestStore store;
  watch::StepTracking steps;
  steps.sample(300, 1000);
  steps.sample(300, 1100);
  assert(steps.count() == 100);
  // Fifty steps occurred asleep after the last visible sample. The one
  // boundary sample finalizes yesterday and baselines today.
  steps.sample(301, 1150);
  assert(steps.dayKey() == 301 && steps.count() == 0);
  assert(!steps.needsBaseline());
  assert(steps.history().count(300) == 150);
  assert(steps.history().find(301));
  steps.sample(301, 1163);
  assert(steps.count() == 13);
  assert(steps.save(store, 10));
  const auto snapshot = reload(store);
  assert(snapshot.history.count(300) == 150);
  assert(snapshot.history.count(301) == 13);
}

void failedMidnightReadPreservesTheCachedOldDayAndNeedsANewBaseline() {
  TestStore store;
  watch::StepTracking steps;
  steps.sample(400, 65000);
  steps.sample(400, 65040);
  assert(steps.save(store, 86350));
  // No raw value is submitted for the failed midnight transaction.
  assert(steps.observeDay(401));
  assert(steps.count() == 0 && steps.needsBaseline());
  assert(steps.history().count(400) == 40);
  assert(!steps.observeDay(401));
  assert(steps.count() == 0 && steps.needsBaseline());
  steps.sample(401, 10);
  assert(steps.count() == 0 && !steps.needsBaseline());
  assert(steps.history().count(400) == 40);
  steps.sample(401, 17);
  assert(steps.count() == 7);
  assert(steps.save(store, 70));
  assert(reload(store).history.count(401) == 7);
}

void backwardsDateCorrectionRestoresRetainedCounts() {
  watch::StepTracking steps;
  steps.sample(500, 1000);
  steps.sample(500, 1100);
  steps.sample(501, 1100);
  steps.sample(501, 1120);
  assert(steps.count() == 20);
  steps.sample(500, 1125);
  assert(steps.history().count(501) == 25);
  assert(steps.count() == 100 && !steps.needsBaseline());
  steps.sample(500, 1130);
  assert(steps.count() == 105);
  steps.sample(501, 1130);
  assert(steps.count() == 25);
  assert(steps.history().count(500) == 105);
  assert(steps.history().latestDay() == 501);
}

void correctingMoreThanTwoWeeksBackAllowsTheNewDayToBeSaved() {
  TestStore store;
  watch::StepTracking steps;
  steps.sample(1000, 100);
  steps.sample(1000, 120);
  assert(steps.save(store, 100));
  steps.sample(985, 125);
  assert(steps.dayKey() == 985 && steps.count() == 0);
  assert(steps.history().size() == 1 && steps.history().latestDay() == 985);
  assert(!steps.history().find(1000));
  steps.sample(985, 128);
  assert(steps.count() == 3);
  assert(steps.save(store, 200));
  const auto snapshot = reload(store);
  assert(snapshot.checkpoint_day == 985 && snapshot.history.count(985) == 3);
}

void savingAfterManyDaysRetainsTodayAndThePreviousFourteen() {
  TestStore store;
  watch::StepTracking steps;
  uint16_t raw = 0;
  for (int32_t day = 2000; day <= 2030; ++day) {
    steps.sample(day, raw);
    raw = static_cast<uint16_t>(raw + day - 1900);
    steps.sample(day, raw);
  }
  assert(steps.history().size() == 15);
  assert(steps.save(store, 3600));
  const auto snapshot = reload(store);
  assert(snapshot.checkpoint_day == 2030 && snapshot.history.size() == 15);
  assert(!snapshot.history.find(2015));
  for (int32_t day = 2016; day <= 2030; ++day) {
    assert(snapshot.history.find(day));
    assert(snapshot.history.count(day) == static_cast<uint32_t>(day - 1900));
  }
}

void failedSaveStaysDirtyAndRetriesOnlyAtTheNextCheckpoint() {
  TestStore store;
  watch::StepTracking steps;
  watch::StepCheckpoint checkpoint;
  checkpoint.begin(0);
  steps.sample(600, 100);
  steps.sample(600, 110);
  assert(steps.save(store, 100));
  steps.sample(600, 115);
  store.fail_write = true;
  const uint32_t first_attempt = watch::StepCheckpoint::kIntervalMs;
  assert(!checkpointIfDue(steps, checkpoint, store, first_attempt,
                          600, 120, 1900));
  assert(steps.dirty() && store.writes == 2);
  assert(reload(store).history.count(600) == 10);
  store.fail_write = false;
  assert(!checkpointIfDue(steps, checkpoint, store, first_attempt + 1,
                          600, 130, 1901));
  assert(store.writes == 2 && steps.count() == 20);
  const uint32_t retry = first_attempt + watch::StepCheckpoint::kIntervalMs;
  assert(checkpointIfDue(steps, checkpoint, store, retry, 600, 140, 3700));
  assert(!steps.dirty() && store.writes == 3);
  assert(reload(store).history.count(600) == 40);
}

void sleepingCheckpointsSampleTheSensorEvenWhenRamWasUnchanged() {
  TestStore store;
  watch::StepTracking steps;
  watch::StepCheckpoint checkpoint;
  checkpoint.begin(10);
  steps.sample(700, 1000);
  assert(steps.save(store, 100));
  assert(!steps.dirty() && steps.count() == 0);
  // RAM sees no visible polling while asleep. The due sensor snapshot must
  // happen before checking dirty(), otherwise these steps are never saved.
  const uint32_t first = 10 + watch::StepCheckpoint::kIntervalMs;
  assert(checkpointIfDue(steps, checkpoint, store, first, 700, 1060, 1900));
  assert(steps.count() == 60 && store.writes == 2);
  assert(reload(store).history.count(700) == 60);
  const uint32_t second = first + watch::StepCheckpoint::kIntervalMs;
  assert(checkpointIfDue(steps, checkpoint, store, second, 700, 1060, 3700));
  assert(store.writes == 2 && !steps.dirty());
}

void invalidDatesDoNotChangeTheLiveTotalOrHistory() {
  watch::StepTracking steps;
  steps.sample(10, 100);
  steps.sample(10, 110);
  const auto before = steps.history();
  assert(!steps.observeDay(-1));
  assert(!steps.observeDay(watch::StepHistory::kLastCivilDay + 1));
  steps.sample(-1, 60000);
  steps.sample(watch::StepHistory::kLastCivilDay + 1, 60000);
  assert(steps.count() == 10 && steps.dayKey() == 10);
  assert(steps.history() == before);
  steps.sample(10, 111);
  assert(steps.count() == 11);
}

}  // namespace

int main() {
  sameDayResetRestoresTheTotalAndTakesAFreshBaseline();
  unchangedTotalsDoNotWriteEvenWhenTheTimestampAdvances();
  midnightClosesTheOldDayWithTheBoundaryRawSample();
  failedMidnightReadPreservesTheCachedOldDayAndNeedsANewBaseline();
  backwardsDateCorrectionRestoresRetainedCounts();
  correctingMoreThanTwoWeeksBackAllowsTheNewDayToBeSaved();
  savingAfterManyDaysRetainsTodayAndThePreviousFourteen();
  failedSaveStaysDirtyAndRetriesOnlyAtTheNextCheckpoint();
  sleepingCheckpointsSampleTheSensorEvenWhenRamWasUnchanged();
  invalidDatesDoNotChangeTheLiveTotalOrHistory();
  std::puts("Step tracking: all 10 scenarios passed.");
}
