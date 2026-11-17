#pragma once

#include "DailySteps.hpp"
#include "StepHistory.hpp"

namespace watch {

// Join sensor baselines to dated history. Raw IMU values never cross a reboot;
// only daily totals do. All live updates stay in RAM until a checkpoint saves.
class StepTracking {
 public:
  void restore(const StepHistory& history) {
    daily_ = DailySteps();
    history_ = history;
    saved_history_ = history;
  }

  bool observeDay(int32_t day) {
    if (day < 0 || day > StepHistory::kLastCivilDay ||
        (daily_.hasDay() && daily_.dayKey() == day)) return false;
    rememberCurrentDay();
    // A correction beyond the retained fortnight starts a new window. Keeping
    // an erroneous far-future date must not prevent saving the corrected day.
    if (history_.hasDay() &&
        day < history_.latestDay() - static_cast<int32_t>(StepHistory::kCapacity - 1))
      history_ = StepHistory();
    daily_.observeDay(day);
    daily_.restoreCount(history_.count(day));
    rememberCurrentDay();
    return true;
  }

  void sample(int32_t day, uint16_t raw) {
    if (day < 0 || day > StepHistory::kLastCivilDay) return;
    if (daily_.hasDay() && daily_.dayKey() != day) {
      // The boundary snapshot closes the outgoing day, including steps taken
      // with the display asleep, and also baselines the incoming day.
      daily_.sample(raw);
      rememberCurrentDay();
    }
    observeDay(day);
    daily_.sample(raw);
    rememberCurrentDay();
  }

  uint32_t count() const { return daily_.count(); }
  bool hasDay() const { return daily_.hasDay(); }
  int32_t dayKey() const { return daily_.dayKey(); }
  bool needsBaseline() const { return daily_.needsBaseline(); }
  const StepHistory& history() const { return history_; }
  bool dirty() const { return history_ != saved_history_; }

  template <typename Store>
  bool save(Store& store, uint32_t second_of_day) {
    if (!dirty()) return true;
    if (!hasDay()) return false;
    StepHistorySnapshot snapshot;
    snapshot.history = history_;
    snapshot.checkpoint_day = dayKey();
    snapshot.checkpoint_second = second_of_day;
    if (!saveStepHistory(store, snapshot)) return false;
    saved_history_ = history_;
    return true;
  }

 private:
  void rememberCurrentDay() {
    if (daily_.hasDay()) history_.upsert(daily_.dayKey(), daily_.count());
  }

  DailySteps daily_;
  StepHistory history_;
  StepHistory saved_history_;
};

}  // namespace watch
