#include "StepCheckpoint.hpp"
#include "StepTracking.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>
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
  assert(restarted.dirty() && restarted.needsAnchorSave());
  assert(restarted.save(store, 43201)); // Commit the independently reset sensor.
  assert(!restarted.dirty() && !restarted.needsAnchorSave());
  restarted.sample(700, 14);
  assert(restarted.count() == 243 && restarted.dirty());
  assert(restarted.save(store, 43260));
  assert(reload(store).history.count(700) == 243 && store.writes == 3);
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
  assert(steps.needsAnchorSave());
  assert(steps.history().count(300) == 150);
  assert(steps.history().find(301));
  steps.sample(301, 1163);
  assert(steps.count() == 13);
  assert(steps.save(store, 10));
  const auto snapshot = reload(store);
  assert(snapshot.history.count(300) == 150);
  assert(snapshot.history.count(301) == 13);
  assert(snapshot.has_raw_steps && snapshot.raw_steps == 1163);
  assert(!steps.needsAnchorSave());
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
  assert(!steps.needsAnchorSave());
  assert(steps.history().count(400) == 40);
  assert(!steps.observeDay(401));
  assert(steps.count() == 0 && steps.needsBaseline());
  steps.sample(401, 10);
  assert(steps.count() == 0 && !steps.needsBaseline());
  assert(steps.needsAnchorSave());
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

void warmResumeRecoversUnsavedStepsAndKeepsCountingDuringBoot() {
  TestStore store;
  watch::StepTracking steps;
  steps.sample(700, 1000);
  steps.sample(700, 1234);
  assert(steps.save(store, 43200));
  const auto snapshot = reload(store);
  assert(snapshot.has_raw_steps && snapshot.raw_steps == 1234);

  watch::StepTracking restarted;
  assert(restarted.resume(snapshot, 1260));
  assert(restarted.count() == 260 && restarted.dayKey() == 700);
  assert(!restarted.needsBaseline() && !restarted.needsAnchorSave());
  assert(restarted.dirty() && store.writes == 1);
  restarted.sample(700, 1265); // More steps while graphics/CTS finish booting.
  assert(restarted.count() == 265);

  watch::StepTracking another_reset;
  assert(another_reset.resume(snapshot, 1265));
  assert(another_reset.count() == 265 && store.writes == 1);
  assert(another_reset.save(store, 43210));
  assert(store.writes == 2);
  watch::StepTracking after_checkpoint;
  assert(after_checkpoint.resume(reload(store), 1270));
  assert(after_checkpoint.count() == 270 && !after_checkpoint.needsAnchorSave());
}

void unchangedWarmResumeNeedsNoAnchorOrFlashWrite() {
  TestStore store;
  watch::StepTracking steps;
  steps.sample(10, 100);
  steps.sample(10, 110);
  assert(steps.save(store, 100));
  const auto snapshot = reload(store);
  watch::StepTracking restarted;
  assert(restarted.resume(snapshot, 110));
  assert(restarted.count() == 10 && !restarted.dirty());
  assert(!restarted.needsBaseline() && !restarted.needsAnchorSave());
  assert(restarted.save(store, 200));
  assert(store.writes == 1);
}

void warmResumeHandlesCounterWrapAndSaturatedTotals() {
  watch::StepHistorySnapshot snapshot;
  snapshot.history.upsert(10, 500);
  snapshot.checkpoint_day = 10;
  snapshot.has_raw_steps = true;
  snapshot.raw_steps = 65530;
  watch::StepTracking steps;
  assert(steps.resume(snapshot, 3));
  assert(steps.count() == 509 && !steps.needsAnchorSave());
  const uint32_t maximum = std::numeric_limits<uint32_t>::max();
  snapshot.history.upsert(10, maximum - 2);
  assert(steps.resume(snapshot, 3));
  assert(steps.count() == maximum);
  steps.sample(10, 4);
  assert(steps.count() == maximum && steps.dirty());
}

void legacyResumeRejectsWithoutMutationAndCanEstablishANewAnchor() {
  watch::StepTracking steps;
  steps.sample(10, 100);
  steps.sample(10, 110);
  const auto history = steps.history();
  watch::StepHistorySnapshot legacy;
  legacy.history.upsert(20, 1000);
  legacy.checkpoint_day = 20;
  assert(!steps.resume(legacy, 120));
  assert(steps.count() == 10 && steps.dayKey() == 10 && steps.history() == history);

  steps.restore(legacy);
  assert(!steps.hasDay() && !steps.dirty());
  steps.observeDay(20);
  assert(steps.count() == 1000 && steps.needsBaseline());
  assert(!steps.needsAnchorSave());
  steps.sample(20, 20);
  assert(steps.needsAnchorSave() && steps.count() == 1000);
  TestStore store;
  assert(steps.save(store, 200));
  const auto migrated = reload(store);
  assert(migrated.has_raw_steps && migrated.raw_steps == 20);
  watch::StepTracking restarted;
  assert(restarted.resume(migrated, 25));
  assert(restarted.count() == 1005);
}

void aColdBaselineMustSaveEvenWhenTheDailyTotalAndRawAreUnchanged() {
  TestStore store;
  watch::StepTracking steps;
  steps.sample(10, 100);
  steps.sample(10, 120);
  assert(steps.save(store, 200));
  const auto snapshot = reload(store);
  steps.restore(snapshot);
  steps.observeDay(10);
  steps.sample(10, 120); // Coincidentally identical, but a new sensor epoch.
  assert(steps.count() == 20 && steps.history() == snapshot.history);
  assert(steps.dirty() && steps.needsAnchorSave());
  assert(steps.save(store, 201));
  assert(store.writes == 2 && !steps.dirty() && !steps.needsAnchorSave());
}

void failedInitialAnchorSaveRemainsPendingUntilDurable() {
  TestStore store;
  watch::StepTracking steps;
  steps.observeDay(10);
  assert(!steps.needsAnchorSave());
  steps.sample(10, 250);
  assert(steps.count() == 0 && steps.needsAnchorSave());
  store.fail_write = true;
  assert(!steps.save(store, 100));
  assert(steps.dirty() && steps.needsAnchorSave() && store.blob.empty());
  steps.sample(10, 260);
  store.fail_write = false;
  assert(steps.save(store, 101));
  const auto saved = reload(store);
  assert(saved.history.count(10) == 10 && saved.raw_steps == 260 && saved.has_raw_steps);
  assert(!steps.dirty() && !steps.needsAnchorSave());
}

void savingWithoutABaselineDoesNotPretendToCommitARawAnchor() {
  TestStore store;
  watch::StepTracking steps;
  steps.observeDay(10);
  assert(steps.save(store, 100));
  assert(!reload(store).has_raw_steps);
  assert(steps.dirty() && !steps.needsAnchorSave());
  steps.sample(10, 250);
  assert(steps.needsAnchorSave());
  assert(steps.save(store, 101));
  assert(reload(store).raw_steps == 250 && !steps.dirty());
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
  warmResumeRecoversUnsavedStepsAndKeepsCountingDuringBoot();
  unchangedWarmResumeNeedsNoAnchorOrFlashWrite();
  warmResumeHandlesCounterWrapAndSaturatedTotals();
  legacyResumeRejectsWithoutMutationAndCanEstablishANewAnchor();
  aColdBaselineMustSaveEvenWhenTheDailyTotalAndRawAreUnchanged();
  failedInitialAnchorSaveRemainsPendingUntilDurable();
  savingWithoutABaselineDoesNotPretendToCommitARawAnchor();
  std::puts("Step tracking: all 17 scenarios passed.");
}
