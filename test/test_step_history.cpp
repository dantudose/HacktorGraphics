#include "DailySteps.hpp"
#include "StepHistory.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

struct TestStore {
  std::map<std::string, std::vector<uint8_t>> blobs;
  size_t writes = 0;
  bool fail_read = false;
  bool fail_write = false;
  bool truncate_write = false;

  size_t getBytesLength(const char* key) const {
    const auto it = blobs.find(key);
    return it == blobs.end() ? 0 : it->second.size();
  }

  size_t getBytes(const char* key, void* destination, size_t size) const {
    const auto it = blobs.find(key);
    if (fail_read || it == blobs.end() || it->second.size() != size) return 0;
    std::memcpy(destination, it->second.data(), size);
    return size;
  }

  size_t putBytes(const char* key, const void* source, size_t size) {
    ++writes;
    if (fail_write) return 0;
    const auto* bytes = static_cast<const uint8_t*>(source);
    const size_t length = truncate_write ? size / 2 : size;
    blobs[key] = std::vector<uint8_t>(bytes, bytes + length);
    return length;
  }
};

watch::StepHistorySnapshot exampleSnapshot() {
  watch::StepHistorySnapshot snapshot;
  snapshot.history.upsert(739935, 9001);
  snapshot.history.upsert(739936, 0);
  snapshot.history.upsert(739937, UINT32_MAX);
  snapshot.checkpoint_day = 739937;
  snapshot.checkpoint_second = 17 * 3600 + 23 * 60 + 45;
  snapshot.raw_steps = 0xa17b;
  snapshot.has_raw_steps = true;
  return snapshot;
}

std::vector<uint8_t> encoded(const watch::StepHistorySnapshot& snapshot) {
  std::vector<uint8_t> bytes(watch::kStepHistoryBlobSize);
  assert(watch::encodeStepHistory(snapshot, bytes.data(), bytes.size()));
  return bytes;
}

void fixChecksum(std::vector<uint8_t>& bytes) {
  using namespace watch::step_history_detail;
  write32(bytes.data() + bytes.size() - 4, crc32(bytes.data(), bytes.size() - 4));
}

// Frozen v1 layout: one record for day42, 1234steps, checkpoint at01:00:00.
// The CRC is a fixture constant, so migration does not depend on the v2 encoder.
std::vector<uint8_t> legacyFixture() {
  std::vector<uint8_t> bytes(watch::kStepHistoryLegacyBlobSize, 0);
  bytes[0] = 'H'; bytes[1] = 'S'; bytes[2] = 'T'; bytes[3] = 'P';
  bytes[4] = 1;
  bytes[6] = 15;
  bytes[8] = 1;
  bytes[12] = 42;
  bytes[16] = 0x10; bytes[17] = 0x0e;
  bytes[20] = 42;
  bytes[24] = 0xd2; bytes[25] = 0x04;
  bytes[140] = 0xbd; bytes[141] = 0x1c;
  bytes[142] = 0x7f; bytes[143] = 0xdb;
  return bytes;
}

void rejectsWithoutMutation(const std::vector<uint8_t>& bytes) {
  auto live = exampleSnapshot();
  const auto before = live;
  assert(!watch::decodeStepHistory(bytes.data(), bytes.size(), live));
  assert(live == before);
}

void emptyHistoryHasNoRecords() {
  watch::StepHistory history;
  assert(history.size() == 0 && !history.hasDay() && history.latestDay() == -1);
  assert(!history.find(0) && history.count(0) == 0);
}

void zeroCountsAreDifferentFromMissingDates() {
  watch::StepHistory history;
  assert(history.upsert(50, 0));
  assert(history.find(50) && history.count(50) == 0);
  assert(!history.find(49) && history.count(49) == 0);
  assert(!history.upsert(50, 0));
  assert(history.upsert(50, 12));
  assert(history.size() == 1 && history.count(50) == 12);
}

void previousTwoWeeksAndTodayAlwaysFit() {
  watch::StepHistory history;
  for (int32_t day = 500; day < 600; ++day) history.upsert(day, uint32_t(day));
  assert(history.size() == 15 && history.latestDay() == 599);
  for (int32_t day = 585; day <= 599; ++day) {
    assert(history.find(day) && history.count(day) == uint32_t(day));
    assert(history.entry(size_t(day - 585)).day == day);
  }
  assert(!history.find(584));
}

void missedCalendarDatesDoNotExtendRetention() {
  watch::StepHistory history;
  history.upsert(100, 1000);
  history.upsert(102, 1020);
  history.upsert(110, 1100);
  history.upsert(115, 1150);
  assert(history.size() == 3 && !history.find(100));
  assert(history.find(102) && history.find(110) && history.find(115));
  assert(!history.find(114));
  history.upsert(200, 2000);
  assert(history.size() == 1 && history.count(200) == 2000);
}

void backwardsCorrectionsKeepFutureCounts() {
  watch::StepHistory history;
  history.upsert(100, 11);
  history.upsert(110, 22);
  assert(history.upsert(105, 33));
  assert(history.upsert(100, 44));
  assert(history.count(100) == 44 && history.count(105) == 33 && history.count(110) == 22);
  assert(history.latestDay() == 110 && history.size() == 3);
  const auto before = history;
  assert(!history.upsert(95, 99));
  assert(history == before);
}

void civilDateEdgesAndInvalidDatesAreBounded() {
  watch::StepHistory history;
  assert(history.upsert(0, 1));
  assert(history.upsert(14, UINT32_MAX));
  assert(history.find(0));
  const auto before = history;
  assert(!history.upsert(-1, 2));
  assert(!history.upsert(INT32_MIN, 2));
  assert(!history.upsert(3652059, 2));
  assert(!history.upsert(INT32_MAX, 2));
  assert(history == before);
  assert(history.upsert(3652058, UINT32_MAX));
  assert(history.size() == 1 && history.count(3652058) == UINT32_MAX);
}

void historyEqualityIgnoresInsertionOrder() {
  watch::StepHistory a;
  watch::StepHistory b;
  a.upsert(100, 42);
  a.upsert(110, 99);
  b.upsert(110, 99);
  b.upsert(100, 42);
  assert(a == b);
  b.upsert(100, 43);
  assert(a != b);
}

void snapshotRoundTripsWithoutFlashWritesDuringLoad() {
  TestStore store;
  const auto original = exampleSnapshot();
  assert(watch::saveStepHistory(store, original));
  assert(store.writes == 1 && store.blobs.size() == 1);
  assert(store.getBytesLength(watch::stepHistoryKey()) == 148);
  watch::StepHistorySnapshot reloaded;
  assert(watch::loadStepHistory(store, reloaded));
  assert(reloaded == original && store.writes == 1);
  // A reset can seed the new software total from its exact calendar entry.
  assert(reloaded.history.count(reloaded.checkpoint_day) == UINT32_MAX);
}

void fullHistoryRoundTripsEveryDateAndCount() {
  watch::StepHistorySnapshot snapshot;
  for (int32_t day = 1000; day <= 1014; ++day)
    snapshot.history.upsert(day, uint32_t(day) * 1717);
  snapshot.checkpoint_day = 1005; // Current clock may have been corrected back.
  snapshot.checkpoint_second = 86399;
  const auto bytes = encoded(snapshot);
  watch::StepHistorySnapshot reloaded;
  assert(watch::decodeStepHistory(bytes.data(), bytes.size(), reloaded));
  assert(reloaded == snapshot && reloaded.history.latestDay() == 1014);
}

void encodingIsDeterministicAndIndependentOfObjectPadding() {
  auto a = exampleSnapshot();
  auto b = exampleSnapshot();
  b.history.upsert(739936, 77);
  b.history.upsert(739936, 0);
  assert(encoded(a) == encoded(b));
  const auto bytes = encoded(a);
  assert(bytes[0] == 'H' && bytes[4] == 2 && bytes[5] == 0);
  assert(bytes[20] == 0x7b && bytes[21] == 0xa1);
  assert(bytes[22] == 1 && bytes[23] == 0);
  for (size_t i = 24 + a.history.size() * 8; i < bytes.size() - 4; ++i)
    assert(bytes[i] == 0);
}

void legacyVersionOneLoadsWithoutInventingARawAnchorOrWriting() {
  TestStore store;
  store.blobs[watch::stepHistoryKey()] = legacyFixture();
  auto loaded = exampleSnapshot(); // Existing live raw anchor must be cleared.
  assert(watch::loadStepHistory(store, loaded));
  assert(loaded.history.size() == 1 && loaded.history.count(42) == 1234);
  assert(loaded.checkpoint_day == 42 && loaded.checkpoint_second == 3600);
  assert(!loaded.has_raw_steps && loaded.raw_steps == 0);
  assert(store.writes == 0);
  assert(store.blobs.at(watch::stepHistoryKey()) == legacyFixture());
  assert(watch::saveStepHistory(store, loaded));
  assert(store.writes == 1 && store.getBytesLength(watch::stepHistoryKey()) == 148);
  assert(store.blobs.at(watch::stepHistoryKey())[4] == 2);
  watch::StepHistorySnapshot migrated;
  assert(watch::loadStepHistory(store, migrated) && migrated == loaded);
}

void rawCounterZeroAndMaximumAreBothValidAnchors() {
  for (uint16_t raw : {uint16_t(0), uint16_t(0xffff)}) {
    auto snapshot = exampleSnapshot();
    snapshot.raw_steps = raw;
    const auto bytes = encoded(snapshot);
    watch::StepHistorySnapshot decoded;
    assert(watch::decodeStepHistory(bytes.data(), bytes.size(), decoded));
    assert(decoded == snapshot && decoded.has_raw_steps && decoded.raw_steps == raw);
  }
  auto absent = exampleSnapshot();
  absent.has_raw_steps = false;
  absent.raw_steps = 0;
  const auto bytes = encoded(absent);
  watch::StepHistorySnapshot decoded;
  assert(watch::decodeStepHistory(bytes.data(), bytes.size(), decoded));
  assert(decoded == absent && !decoded.has_raw_steps);
}

void snapshotEqualityIncludesTheRawAnchor() {
  const auto original = exampleSnapshot();
  auto changed = original;
  changed.raw_steps ^= 1;
  assert(changed != original);
  changed = original;
  changed.has_raw_steps = false;
  assert(changed != original);
}

void invalidRawFlagsReservedBytesAndNoncanonicalAnchorsAreRejected() {
  const auto original = encoded(exampleSnapshot());
  for (size_t offset : {size_t(22), size_t(23)}) {
    auto damaged = original;
    damaged[offset] |= 2;
    fixChecksum(damaged);
    rejectsWithoutMutation(damaged);
  }
  auto missing_flag = original;
  missing_flag[22] = 0; // A nonzero raw counter without a valid flag is ambiguous.
  fixChecksum(missing_flag);
  rejectsWithoutMutation(missing_flag);
  TestStore store;
  auto noncanonical = exampleSnapshot();
  noncanonical.has_raw_steps = false;
  assert(!watch::saveStepHistory(store, noncanonical) && store.writes == 0);
}

void emptyOrInvalidClockSnapshotsAreNeverWritten() {
  TestStore store;
  watch::StepHistorySnapshot empty;
  assert(!watch::saveStepHistory(store, empty) && store.writes == 0);
  auto snapshot = exampleSnapshot();
  snapshot.checkpoint_day = 7;
  assert(!watch::saveStepHistory(store, snapshot) && store.writes == 0);
  snapshot = exampleSnapshot();
  snapshot.checkpoint_second = 86400;
  assert(!watch::saveStepHistory(store, snapshot) && store.writes == 0);
}

void missingOrShortReadsPreserveLiveState() {
  TestStore store;
  auto live = exampleSnapshot();
  const auto before = live;
  assert(!watch::loadStepHistory(store, live) && live == before && store.writes == 0);
  store.blobs[watch::stepHistoryKey()] = {1, 2, 3};
  assert(!watch::loadStepHistory(store, live) && live == before && store.writes == 0);
  store.blobs[watch::stepHistoryKey()] = encoded(before);
  store.fail_read = true;
  assert(!watch::loadStepHistory(store, live) && live == before && store.writes == 0);
}

void everySingleBitCorruptionFailsIntegrityCheck() {
  const auto original = encoded(exampleSnapshot());
  for (size_t byte = 0; byte < original.size(); ++byte) {
    for (unsigned bit = 0; bit < 8; ++bit) {
      auto damaged = original;
      damaged[byte] ^= uint8_t(1U << bit);
      rejectsWithoutMutation(damaged);
    }
  }
}

void unsupportedVersionsAndMalformedHeadersAreRejected() {
  const auto original = encoded(exampleSnapshot());
  for (size_t offset : {size_t(0), size_t(4), size_t(5), size_t(6), size_t(7)}) {
    auto damaged = original;
    damaged[offset] ^= 3;
    fixChecksum(damaged);
    rejectsWithoutMutation(damaged);
  }
  for (uint32_t count : {0U, 16U, UINT32_MAX}) {
    auto damaged = original;
    watch::step_history_detail::write32(damaged.data() + 8, count);
    fixChecksum(damaged);
    rejectsWithoutMutation(damaged);
  }
}

void invalidCheckpointDatesAndSecondsAreRejected() {
  const auto original = encoded(exampleSnapshot());
  for (uint32_t day : {7U, 3652059U, UINT32_MAX}) {
    auto damaged = original;
    watch::step_history_detail::write32(damaged.data() + 12, day);
    fixChecksum(damaged);
    rejectsWithoutMutation(damaged);
  }
  for (uint32_t second : {86400U, UINT32_MAX}) {
    auto damaged = original;
    watch::step_history_detail::write32(damaged.data() + 16, second);
    fixChecksum(damaged);
    rejectsWithoutMutation(damaged);
  }
}

void invalidDuplicateUnsortedAndStaleRecordsAreRejected() {
  const auto original = encoded(exampleSnapshot());
  for (uint32_t day : {739936U, 739938U, 3652059U, UINT32_MAX, 739000U}) {
    auto damaged = original;
    watch::step_history_detail::write32(damaged.data() + 24, day);
    fixChecksum(damaged);
    rejectsWithoutMutation(damaged);
  }
  auto unused = original;
  unused[24 + 3 * 8] = 1;
  fixChecksum(unused);
  rejectsWithoutMutation(unused);
}

void failedSavePreservesOldSnapshotAndCanBeRetried() {
  TestStore store;
  const auto original = exampleSnapshot();
  assert(watch::saveStepHistory(store, original));
  auto changed = original;
  changed.history.upsert(changed.checkpoint_day, 1234);
  store.fail_write = true;
  assert(!watch::saveStepHistory(store, changed));
  watch::StepHistorySnapshot reloaded;
  assert(watch::loadStepHistory(store, reloaded) && reloaded == original);
  store.fail_write = false;
  assert(watch::saveStepHistory(store, changed));
  assert(watch::loadStepHistory(store, reloaded) && reloaded == changed);
  assert(store.writes == 3);
}

void partialWriteIsReportedAndTruncatedBlobIsRejected() {
  TestStore store;
  const auto original = exampleSnapshot();
  store.truncate_write = true;
  assert(!watch::saveStepHistory(store, original));
  auto live = original;
  assert(!watch::loadStepHistory(store, live) && live == original);
  assert(store.writes == 1);
}

void wrongBufferSizesAndNullPointersAreRejected() {
  const auto snapshot = exampleSnapshot();
  auto bytes = encoded(snapshot);
  assert(!watch::encodeStepHistory(snapshot, nullptr, bytes.size()));
  assert(!watch::encodeStepHistory(snapshot, bytes.data(), bytes.size() - 1));
  auto live = snapshot;
  assert(!watch::decodeStepHistory(nullptr, bytes.size(), live));
  bytes.pop_back();
  rejectsWithoutMutation(bytes);
  bytes.push_back(0);
  bytes.push_back(0);
  rejectsWithoutMutation(bytes);
}

} // namespace

int main() {
  emptyHistoryHasNoRecords();
  zeroCountsAreDifferentFromMissingDates();
  previousTwoWeeksAndTodayAlwaysFit();
  missedCalendarDatesDoNotExtendRetention();
  backwardsCorrectionsKeepFutureCounts();
  civilDateEdgesAndInvalidDatesAreBounded();
  historyEqualityIgnoresInsertionOrder();
  snapshotRoundTripsWithoutFlashWritesDuringLoad();
  fullHistoryRoundTripsEveryDateAndCount();
  encodingIsDeterministicAndIndependentOfObjectPadding();
  legacyVersionOneLoadsWithoutInventingARawAnchorOrWriting();
  rawCounterZeroAndMaximumAreBothValidAnchors();
  snapshotEqualityIncludesTheRawAnchor();
  invalidRawFlagsReservedBytesAndNoncanonicalAnchorsAreRejected();
  emptyOrInvalidClockSnapshotsAreNeverWritten();
  missingOrShortReadsPreserveLiveState();
  everySingleBitCorruptionFailsIntegrityCheck();
  unsupportedVersionsAndMalformedHeadersAreRejected();
  invalidCheckpointDatesAndSecondsAreRejected();
  invalidDuplicateUnsortedAndStaleRecordsAreRejected();
  failedSavePreservesOldSnapshotAndCanBeRetried();
  partialWriteIsReportedAndTruncatedBlobIsRejected();
  wrongBufferSizesAndNullPointersAreRejected();
  std::puts("Step history: all 23 scenarios passed.");
}
