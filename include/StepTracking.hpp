#pragma once

#include "DailySteps.hpp"
#include "StepHistory.hpp"

namespace watch {

// Join sensor baselines to dated history. A persisted raw anchor can recover
// unsaved activity only after startup verifies the same running sensor epoch.
// Ordinary live updates stay in RAM until the next periodic checkpoint.
class StepTracking {
 public:
  void restore(const StepHistorySnapshot& snapshot) {
    daily_ = DailySteps();
    history_ = snapshot.history;
    saved_history_ = snapshot.history;
    saved_raw_ = snapshot.raw_steps;
    saved_has_raw_ = snapshot.has_raw_steps;
    anchor_required_ = false;
  }

  void restore(const StepHistory& history) {
    StepHistorySnapshot snapshot;
    snapshot.history = history;
    restore(snapshot);
  }

  // The caller must prove that live_raw continues the saved hardware counter.
  // A raw-less legacy snapshot cannot establish that relationship.
  bool resume(const StepHistorySnapshot& snapshot, uint16_t live_raw) {
    if (!snapshot.has_raw_steps ||
        !snapshot.history.find(snapshot.checkpoint_day)) return false;
    restore(snapshot);
    daily_.observeDay(snapshot.checkpoint_day);
    daily_.restoreCountAndRaw(history_.count(snapshot.checkpoint_day), snapshot.raw_steps);
    daily_.sample(live_raw);
    rememberCurrentDay();
    return true;
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
    anchor_required_ = true;
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
  bool needsAnchorSave() const {
    return anchor_required_ && hasDay() && !needsBaseline();
  }
  const StepHistory& history() const { return history_; }
  bool dirty() const {
    if (history_ != saved_history_ || anchor_required_) return true;
    if (!hasDay()) return false;
    const bool has_raw = !needsBaseline();
    return has_raw != saved_has_raw_ || (has_raw && daily_.lastRaw() != saved_raw_);
  }

  template <typename Store>
  bool save(Store& store, uint32_t second_of_day) {
    if (!dirty()) return true;
    if (!hasDay()) return false;
    StepHistorySnapshot snapshot;
    snapshot.history = history_;
    snapshot.checkpoint_day = dayKey();
    snapshot.checkpoint_second = second_of_day;
    snapshot.has_raw_steps = !needsBaseline();
    if (snapshot.has_raw_steps) snapshot.raw_steps = daily_.lastRaw();
    if (!saveStepHistory(store, snapshot)) return false;
    saved_history_ = history_;
    saved_raw_ = snapshot.raw_steps;
    saved_has_raw_ = snapshot.has_raw_steps;
    if (snapshot.has_raw_steps) anchor_required_ = false;
    return true;
  }

 private:
  void rememberCurrentDay() {
    if (daily_.hasDay()) history_.upsert(daily_.dayKey(), daily_.count());
  }

  DailySteps daily_;
  StepHistory history_;
  StepHistory saved_history_;
  uint16_t saved_raw_ = 0;
  bool saved_has_raw_ = false;
  bool anchor_required_ = false;
};

}  // namespace watch
