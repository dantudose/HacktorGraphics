#include "DailySteps.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace {

void startupNeedsADateAndSuccessfulBaseline() {
  watch::DailySteps steps;
  assert(!steps.hasDay());
  assert(steps.needsBaseline());
  assert(steps.count() == 0);
  steps.sample(1234); // A sample with no calendar date cannot count anything.
  assert(steps.needsBaseline());
  assert(steps.observeDay(watch::civilDayNumber(2026, 11, 5)));
  assert(steps.hasDay());
  steps.sample(5678);
  assert(!steps.needsBaseline());
  assert(steps.count() == 0);
  steps.sample(5687);
  assert(steps.count() == 9);
}

void unchangedRawSamplesDoNotAddSteps() {
  watch::DailySteps steps;
  steps.observeDay(1);
  steps.sample(400);
  steps.sample(400);
  assert(steps.count() == 0);
  steps.sample(411);
  steps.sample(411);
  assert(steps.count() == 11);
}

void sameCivilDateSurvivesClockReanchoring() {
  watch::DailySteps steps;
  const int32_t day = watch::civilDayNumber(2026, 11, 5);
  steps.observeDay(day);
  steps.sample(1200);
  steps.sample(1300);
  // A CTS correction changes clock anchors but retains this absolute day key.
  assert(!steps.observeDay(day));
  assert(!steps.needsBaseline());
  assert(steps.count() == 100);
  steps.sample(1301);
  assert(steps.count() == 101);
}

void midnightStartsAtSnapshotWithoutResettingTheSensor() {
  watch::DailySteps steps;
  steps.observeDay(20);
  steps.sample(1000);
  steps.sample(1400);
  assert(steps.count() == 400);
  assert(steps.observeDay(21));
  assert(steps.count() == 0);
  assert(steps.needsBaseline());
  steps.sample(1423); // Includes old-day steps since the last visible poll.
  assert(steps.count() == 0);
  steps.sample(1430);
  assert(steps.count() == 7);
  assert(!steps.observeDay(21));
  assert(steps.count() == 7);
}

void failedBoundaryReadsLeaveTheBaselinePending() {
  watch::DailySteps steps;
  steps.observeDay(1);
  steps.sample(50);
  steps.sample(75);
  steps.observeDay(2);
  // Failed transactions do not call sample(), including repeated day checks.
  assert(!steps.observeDay(2));
  assert(!steps.observeDay(2));
  assert(steps.needsBaseline());
  assert(steps.count() == 0);
  steps.sample(100);
  assert(!steps.needsBaseline());
  steps.sample(104);
  assert(steps.count() == 4);
}

void backwardsDateCorrectionStartsTheDisplayedDateAgain() {
  watch::DailySteps steps;
  steps.observeDay(10);
  steps.sample(100);
  steps.sample(120);
  assert(steps.observeDay(9));
  assert(steps.count() == 0);
  assert(steps.needsBaseline());
  steps.sample(125);
  steps.sample(130);
  assert(steps.count() == 5);
  assert(steps.observeDay(10));
  assert(steps.count() == 0);
}

void multipleMissedDaysNeedOneNewBaseline() {
  watch::DailySteps steps;
  steps.observeDay(10);
  steps.sample(100);
  steps.sample(200);
  assert(steps.observeDay(15));
  assert(steps.count() == 0);
  steps.sample(10000);
  steps.sample(10002);
  assert(steps.count() == 2);
}

void sensorWrapAddsOnlyTheActualDelta() {
  watch::DailySteps steps;
  steps.observeDay(1);
  steps.sample(65530);
  steps.sample(2);
  assert(steps.count() == 8);
  steps.sample(10);
  assert(steps.count() == 16);
}

void dailyTotalCanExceedSensorCounterWidth() {
  watch::DailySteps steps;
  steps.observeDay(1);
  steps.sample(0);
  steps.sample(40000);
  steps.sample(14464); // Another 40000 steps across a sensor wrap.
  assert(steps.count() == 80000);
}

void corruptedRunawayTotalsSaturateInsteadOfWrapping() {
  watch::DailySteps steps;
  steps.observeDay(1);
  uint16_t raw = 0;
  steps.sample(raw);
  for (uint32_t i = 0; i < 65538; ++i) {
    raw = static_cast<uint16_t>(raw - 1); // Each sample is a 65535-step delta.
    steps.sample(raw);
  }
  assert(steps.count() == std::numeric_limits<uint32_t>::max());
  steps.sample(raw);
  assert(steps.count() == std::numeric_limits<uint32_t>::max());
}

void restoringWithoutADateDoesNotCreateADailyTotal() {
  watch::DailySteps steps;
  steps.restoreCount(2000);
  assert(!steps.hasDay());
  assert(steps.needsBaseline());
  assert(steps.count() == 0);
  steps.observeDay(20);
  assert(steps.dayKey() == 20);
  assert(steps.count() == 0);
}

void restoredTotalSurvivesFailedReadsAndAFreshBaseline() {
  watch::DailySteps steps;
  const int32_t day = watch::civilDayNumber(2026, 11, 17);
  steps.observeDay(day);
  steps.restoreCount(4321);
  assert(steps.dayKey() == day);
  assert(steps.count() == 4321);
  assert(steps.needsBaseline());
  // I2C failures do not call sample(); date observations must preserve restore.
  assert(!steps.observeDay(day));
  assert(!steps.observeDay(day));
  assert(steps.count() == 4321 && steps.needsBaseline());
  steps.sample(65530); // Sensor counter has no relation to the saved daily sum.
  assert(steps.count() == 4321);
  assert(!steps.needsBaseline());
  steps.sample(2); // Fresh baseline also retains ordinary 16-bit wrap handling.
  assert(steps.count() == 4329);
  steps.sample(8);
  assert(steps.count() == 4335);
}

void restoringAnEstablishedDayDiscardsTheOldRawBaseline() {
  watch::DailySteps steps;
  steps.observeDay(10);
  steps.sample(30000);
  steps.sample(30100);
  assert(steps.count() == 100);
  steps.restoreCount(900);
  assert(steps.dayKey() == 10);
  assert(steps.needsBaseline());
  steps.sample(3); // A reset sensor must not produce a huge wrapped delta.
  assert(steps.count() == 900);
  steps.sample(13);
  assert(steps.count() == 910);
}

void changingDatesStillClearsARestoredTotal() {
  watch::DailySteps steps;
  steps.observeDay(10);
  steps.restoreCount(900);
  steps.sample(1000);
  assert(steps.observeDay(11));
  assert(steps.dayKey() == 11);
  assert(steps.count() == 0);
  assert(steps.needsBaseline());
  steps.sample(1003);
  assert(steps.count() == 0);
  steps.sample(1010);
  assert(steps.count() == 7);
}

void restoredTotalSaturatesInsteadOfWrapping() {
  watch::DailySteps steps;
  steps.observeDay(1);
  const uint32_t maximum = std::numeric_limits<uint32_t>::max();
  steps.restoreCount(maximum - 2);
  steps.sample(100);
  assert(steps.count() == maximum - 2);
  steps.sample(103);
  assert(steps.count() == maximum);
  steps.sample(110);
  assert(steps.count() == maximum);
}

void verifiedRawRestoreRequiresADateAndAddsTheFirstDelta() {
  watch::DailySteps steps;
  steps.restoreCountAndRaw(2000, 1234);
  assert(!steps.hasDay() && steps.count() == 0 && steps.needsBaseline());
  steps.observeDay(10);
  steps.restoreCountAndRaw(234, 1234);
  assert(steps.count() == 234 && !steps.needsBaseline());
  assert(steps.lastRaw() == 1234);
  steps.sample(1260);
  assert(steps.count() == 260 && steps.lastRaw() == 1260);
}

void verifiedRawRestoreHandlesWrapAndLaterDateChanges() {
  watch::DailySteps steps;
  steps.observeDay(10);
  steps.restoreCountAndRaw(900, 65530);
  steps.sample(3);
  assert(steps.count() == 909 && steps.lastRaw() == 3);
  assert(steps.observeDay(11));
  assert(steps.count() == 0 && steps.needsBaseline());
  steps.sample(7);
  assert(steps.count() == 0 && steps.lastRaw() == 7);
}

void verifiedRawRestoreCanReplaceABaselineAndStillSaturates() {
  watch::DailySteps steps;
  steps.observeDay(1);
  steps.sample(100);
  steps.sample(110);
  const uint32_t maximum = std::numeric_limits<uint32_t>::max();
  steps.restoreCountAndRaw(maximum - 2, 2000);
  steps.sample(2005);
  assert(steps.count() == maximum && steps.lastRaw() == 2005);
}

void civilDatesUseAnAbsoluteGregorianOrdinal() {
  using watch::civilDayNumber;
  assert(civilDayNumber(1, 1, 1) == 0);
  assert(civilDayNumber(1970, 1, 1) == 719162);
  assert(civilDayNumber(2000, 1, 1) == 730119);
  assert(civilDayNumber(9999, 12, 31) == 3652058);
  assert(civilDayNumber(2027, 1, 1) - civilDayNumber(2026, 12, 31) == 1);
  assert(civilDayNumber(2026, 12, 1) - civilDayNumber(2026, 11, 30) == 1);
}

void leapAndCenturyBoundariesStayAdjacent() {
  using watch::civilDayNumber;
  assert(civilDayNumber(2024, 2, 29) - civilDayNumber(2024, 2, 28) == 1);
  assert(civilDayNumber(2024, 3, 1) - civilDayNumber(2024, 2, 29) == 1);
  assert(civilDayNumber(2025, 3, 1) - civilDayNumber(2025, 2, 28) == 1);
  assert(civilDayNumber(1900, 3, 1) - civilDayNumber(1900, 2, 28) == 1);
  assert(civilDayNumber(2000, 3, 1) - civilDayNumber(2000, 2, 28) == 2);
  assert(civilDayNumber(2100, 3, 1) - civilDayNumber(2100, 2, 28) == 1);
  assert(civilDayNumber(2400, 3, 1) - civilDayNumber(2400, 2, 28) == 2);
}

void dayOrdinalsRecoverEndpointsAndLeapDates() {
  struct Example {
    int32_t ordinal;
    int year;
    int month;
    int day;
  };
  const Example examples[] = {
      {0, 1, 1, 1}, {364, 1, 12, 31}, {365, 2, 1, 1},
      {719162, 1970, 1, 1}, {730119, 2000, 1, 1},
      {730177, 2000, 2, 28}, {730178, 2000, 2, 29},
      {730179, 2000, 3, 1}, {3652058, 9999, 12, 31}};
  for (const auto& example : examples) {
    const auto date = watch::civilDateFromDayNumber(example.ordinal);
    assert(date.year == example.year && date.month == example.month &&
           date.day == example.day);
  }
}

void civilDateRoundtripCoversTwoCenturiesOfEveryDate() {
  static const int month_days[] = {
      31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int32_t ordinal = watch::civilDayNumber(1899, 1, 1);
  for (int year = 1899; year <= 2101; ++year) {
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    for (int month = 1; month <= 12; ++month) {
      const int days = month_days[month - 1] + (month == 2 && leap ? 1 : 0);
      for (int day = 1; day <= days; ++day, ++ordinal) {
        const auto date = watch::civilDateFromDayNumber(ordinal);
        assert(date.year == year && date.month == month && date.day == day);
        assert(watch::civilDayNumber(date.year, date.month, date.day) == ordinal);
      }
    }
  }
  assert(ordinal == watch::civilDayNumber(2102, 1, 1));
}

void midnightWaitUsesClockTimeIncludingElapsedDays() {
  using watch::millisUntilMidnight;
  assert(millisUntilMidnight(0) == 86400000);
  assert(millisUntilMidnight(1) == 86399999);
  assert(millisUntilMidnight(43200000) == 43200000);
  assert(millisUntilMidnight(86399999) == 1);
  assert(millisUntilMidnight(86400000) == 86400000);
  assert(millisUntilMidnight(86400001) == 86399999);
  const uint64_t days = 3652058ULL;
  assert(millisUntilMidnight(days * 86400000ULL + 86399999ULL) == 1);
  assert(millisUntilMidnight(days * 86400000ULL + 1234ULL) == 86398766);
}

}  // namespace

int main() {
  startupNeedsADateAndSuccessfulBaseline();
  unchangedRawSamplesDoNotAddSteps();
  sameCivilDateSurvivesClockReanchoring();
  midnightStartsAtSnapshotWithoutResettingTheSensor();
  failedBoundaryReadsLeaveTheBaselinePending();
  backwardsDateCorrectionStartsTheDisplayedDateAgain();
  multipleMissedDaysNeedOneNewBaseline();
  sensorWrapAddsOnlyTheActualDelta();
  dailyTotalCanExceedSensorCounterWidth();
  corruptedRunawayTotalsSaturateInsteadOfWrapping();
  restoringWithoutADateDoesNotCreateADailyTotal();
  restoredTotalSurvivesFailedReadsAndAFreshBaseline();
  restoringAnEstablishedDayDiscardsTheOldRawBaseline();
  changingDatesStillClearsARestoredTotal();
  restoredTotalSaturatesInsteadOfWrapping();
  verifiedRawRestoreRequiresADateAndAddsTheFirstDelta();
  verifiedRawRestoreHandlesWrapAndLaterDateChanges();
  verifiedRawRestoreCanReplaceABaselineAndStillSaturates();
  civilDatesUseAnAbsoluteGregorianOrdinal();
  leapAndCenturyBoundariesStayAdjacent();
  dayOrdinalsRecoverEndpointsAndLeapDates();
  civilDateRoundtripCoversTwoCenturiesOfEveryDate();
  midnightWaitUsesClockTimeIncludingElapsedDays();
  std::puts("Daily steps: all 23 scenarios passed.");
}
