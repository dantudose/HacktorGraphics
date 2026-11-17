#pragma once

#include <cstddef>
#include <cstdint>

namespace watch {

struct StepDay {
  int32_t day = 0;
  uint32_t steps = 0;
};

inline bool operator==(const StepDay& a, const StepDay& b) {
  return a.day == b.day && a.steps == b.steps;
}

// Today plus the preceding fourteen calendar dates. The newest date ever
// observed anchors retention, so correcting the clock backward cannot discard
// newer records. Dates older than that window are deliberately ignored.
class StepHistory {
 public:
  static constexpr size_t kCapacity = 15;
  static constexpr int32_t kLastCivilDay = 3652058; // 9999-12-31.

  bool upsert(int32_t day, uint32_t steps) {
    if (day < 0 || day > kLastCivilDay) return false;
    if (size_ && day < latestDay() - static_cast<int32_t>(kCapacity - 1))
      return false;

    for (size_t i = 0; i < size_; ++i) {
      if (days_[i].day == day) {
        if (days_[i].steps == steps) return false;
        days_[i].steps = steps;
        return true;
      }
    }

    const int32_t oldest = day - static_cast<int32_t>(kCapacity - 1);
    size_t removed = 0;
    while (removed < size_ && days_[removed].day < oldest) ++removed;
    if (removed) {
      for (size_t i = removed; i < size_; ++i) days_[i - removed] = days_[i];
      size_ -= removed;
    }
    size_t position = 0;
    while (position < size_ && days_[position].day < day) ++position;
    // At most fifteen distinct integer dates fit the retained window.
    for (size_t i = size_; i > position; --i) days_[i] = days_[i - 1];
    days_[position].day = day;
    days_[position].steps = steps;
    ++size_;
    return true;
  }

  const StepDay* find(int32_t day) const {
    for (size_t i = 0; i < size_; ++i)
      if (days_[i].day == day) return &days_[i];
    return nullptr;
  }

  uint32_t count(int32_t day) const {
    const StepDay* value = find(day);
    return value ? value->steps : 0;
  }

  bool hasDay() const { return size_ != 0; }
  size_t size() const { return size_; }
  int32_t latestDay() const { return size_ ? days_[size_ - 1].day : -1; }
  const StepDay& entry(size_t index) const { return days_[index]; }

 private:
  StepDay days_[kCapacity];
  size_t size_ = 0;
};

inline bool operator==(const StepHistory& a, const StepHistory& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (!(a.entry(i) == b.entry(i))) return false;
  return true;
}

inline bool operator!=(const StepHistory& a, const StepHistory& b) {
  return !(a == b);
}

struct StepHistorySnapshot {
  StepHistory history;
  // The watch's displayed date/time at the last successful checkpoint is a
  // fallback clock anchor after reset; it cannot measure time without power.
  int32_t checkpoint_day = 0;
  uint32_t checkpoint_second = 0;
};

inline bool operator==(const StepHistorySnapshot& a, const StepHistorySnapshot& b) {
  return a.history == b.history && a.checkpoint_day == b.checkpoint_day &&
      a.checkpoint_second == b.checkpoint_second;
}

inline bool operator!=(const StepHistorySnapshot& a, const StepHistorySnapshot& b) {
  return !(a == b);
}

// Little-endian v1: magic[4], version u16, capacity u16, count u32,
// checkpoint day u32, checkpoint second u32, fifteen (day u32, steps u32)
// records, then an IEEE CRC-32 over all preceding bytes. Unused records are
// zero. Explicit encoding avoids depending on C++ structure padding or ABI.
static constexpr size_t kStepHistoryBlobSize = 144;

inline const char* stepHistoryKey() { return "steps-v1"; }

namespace step_history_detail {

inline void write32(uint8_t* bytes, uint32_t value) {
  for (size_t i = 0; i < 4; ++i) bytes[i] = static_cast<uint8_t>(value >> (i * 8));
}

inline uint32_t read32(const uint8_t* bytes) {
  uint32_t value = 0;
  for (size_t i = 0; i < 4; ++i) value |= uint32_t(bytes[i]) << (i * 8);
  return value;
}

inline uint32_t crc32(const uint8_t* bytes, size_t size) {
  uint32_t crc = UINT32_MAX;
  for (size_t i = 0; i < size; ++i) {
    crc ^= bytes[i];
    for (unsigned bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ ((crc & 1U) ? 0xedb88320U : 0U);
  }
  return ~crc;
}

} // namespace step_history_detail

inline bool encodeStepHistory(const StepHistorySnapshot& snapshot,
                              uint8_t* bytes, size_t size) {
  if (!bytes || size != kStepHistoryBlobSize ||
      !snapshot.history.find(snapshot.checkpoint_day) ||
      snapshot.checkpoint_second >= 86400) return false;
  for (size_t i = 0; i < size; ++i) bytes[i] = 0;
  bytes[0] = 'H'; bytes[1] = 'S'; bytes[2] = 'T'; bytes[3] = 'P';
  bytes[4] = 1; // Version u16.
  bytes[6] = static_cast<uint8_t>(StepHistory::kCapacity); // Capacity u16.
  using namespace step_history_detail;
  write32(bytes + 8, static_cast<uint32_t>(snapshot.history.size()));
  write32(bytes + 12, static_cast<uint32_t>(snapshot.checkpoint_day));
  write32(bytes + 16, snapshot.checkpoint_second);
  for (size_t i = 0; i < snapshot.history.size(); ++i) {
    const StepDay& day = snapshot.history.entry(i);
    write32(bytes + 20 + i * 8, static_cast<uint32_t>(day.day));
    write32(bytes + 24 + i * 8, day.steps);
  }
  write32(bytes + size - 4, crc32(bytes, size - 4));
  return true;
}

// Decode into a temporary so every rejection leaves the caller's state intact.
inline bool decodeStepHistory(const uint8_t* bytes, size_t size,
                              StepHistorySnapshot& snapshot) {
  using namespace step_history_detail;
  if (!bytes || size != kStepHistoryBlobSize || bytes[0] != 'H' ||
      bytes[1] != 'S' || bytes[2] != 'T' || bytes[3] != 'P' ||
      bytes[4] != 1 || bytes[5] != 0 ||
      bytes[6] != StepHistory::kCapacity || bytes[7] != 0 ||
      read32(bytes + size - 4) != crc32(bytes, size - 4)) return false;

  const uint32_t count = read32(bytes + 8);
  const uint32_t checkpoint_day = read32(bytes + 12);
  if (count == 0 || count > StepHistory::kCapacity ||
      checkpoint_day > static_cast<uint32_t>(StepHistory::kLastCivilDay) ||
      read32(bytes + 16) >= 86400) return false;

  StepHistorySnapshot decoded;
  int32_t previous = -1;
  for (size_t i = 0; i < StepHistory::kCapacity; ++i) {
    const uint32_t day = read32(bytes + 20 + i * 8);
    const uint32_t steps = read32(bytes + 24 + i * 8);
    if (i >= count) {
      if (day || steps) return false;
      continue;
    }
    if (day > static_cast<uint32_t>(StepHistory::kLastCivilDay) ||
        static_cast<int32_t>(day) <= previous) return false;
    decoded.history.upsert(static_cast<int32_t>(day), steps);
    previous = static_cast<int32_t>(day);
  }
  // upsert prunes out-of-window dates, which makes this a structural check.
  if (decoded.history.size() != count ||
      !decoded.history.find(static_cast<int32_t>(checkpoint_day))) return false;
  decoded.checkpoint_day = static_cast<int32_t>(checkpoint_day);
  decoded.checkpoint_second = read32(bytes + 16);
  snapshot = decoded;
  return true;
}

// Store matches Arduino Preferences. Reading, including a corrupt/missing
// value, never writes or repairs flash. Rate limiting belongs to the caller.
template <typename Store>
bool loadStepHistory(Store& store, StepHistorySnapshot& snapshot) {
  if (store.getBytesLength(stepHistoryKey()) != kStepHistoryBlobSize) return false;
  uint8_t bytes[kStepHistoryBlobSize];
  if (store.getBytes(stepHistoryKey(), bytes, sizeof(bytes)) != sizeof(bytes))
    return false;
  return decodeStepHistory(bytes, sizeof(bytes), snapshot);
}

// One NVS blob keeps the snapshot together; Preferences commits that blob in
// one transaction. A failed write is reported so the caller retains its dirty
// state and retries only after the next permitted interval.
template <typename Store>
bool saveStepHistory(Store& store, const StepHistorySnapshot& snapshot) {
  uint8_t bytes[kStepHistoryBlobSize];
  if (!encodeStepHistory(snapshot, bytes, sizeof(bytes))) return false;
  return store.putBytes(stepHistoryKey(), bytes, sizeof(bytes)) == sizeof(bytes);
}

} // namespace watch
