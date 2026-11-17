// Native: c++ -std=c++11 -Wall -Wextra -Werror -Itest/haptics_stubs -Iinclude
//         test/test_watch_haptics.cpp -o /tmp/hacktor-haptics-tests
#include "WatchHaptics.hpp"

#include <cassert>
#include <cstdio>
#include <initializer_list>

namespace {

void unavailableDriverDoesNotUseTheBus() {
  watch::WatchHaptics driver;
  bool playing = true;
  assert(!driver.available());
  assert(!driver.startConfirmation());
  assert(!driver.startSixPulse());
  assert(!driver.standby());
  assert(!driver.readPlaying(playing) && playing);
}

void initializationNeverStartsTheMotor() {
  watch::WatchHaptics driver;
  TwoWire bus;
  assert(driver.begin(bus) && driver.available());
  assert(bus.regs[1] == 0x40 && bus.regs[2] == 0 && bus.regs[0x0c] == 0);
  assert(bus.regs[0x17] * 21.96 <= 2500.0);
  assert((bus.regs[0x1a] & 0x80) == 0);     // ERM, not an assumed LRA.
  assert((bus.regs[0x1b] & 0x80) == 0);     // No startup boost.
  assert((bus.regs[0x1d] & 0x28) == 0x20);  // Signed, open-loop input.
  for (const auto& write : bus.writes) {
    assert(write.first != 0x0c || write.second != 1);
  }
  assert(bus.timeout == 173);
}

unsigned goCount(const TwoWire& bus) {
  unsigned count = 0;
  for (const auto& write : bus.writes) {
    if (write.first == 0x0c && write.second == 1) ++count;
  }
  return count;
}

void countBatch(const TwoWire& bus, bool trailing_wait,
                unsigned& pulses, unsigned& waits) {
  const uint8_t expected[] = {1, 0x8f, 1, 0x8f, 1,
                             static_cast<uint8_t>(trailing_wait ? 0x8f : 0), 0, 0};
  for (unsigned slot = 0; slot < 8; ++slot) {
    assert(bus.regs[4 + slot] == expected[slot]);
    if (expected[slot] == 1) ++pulses;
    if (expected[slot] & 0x80) {
      ++waits;
      assert((expected[slot] & 127) * 10 == 150);
    }
  }
}

void hardwareSequenceHasExactlySixClicksAndStops() {
  watch::WatchHaptics driver;
  TwoWire bus;
  assert(driver.begin(bus));
  const auto first = bus.writes.size();
  assert(driver.startSixPulse());
  unsigned pulses = 0;
  unsigned waits = 0;
  countBatch(bus, true, pulses, waits);
  // Exit standby before loading the complete sequence, then GO last.
  assert(bus.writes[first] == std::make_pair(uint8_t(0x0c), uint8_t(0)));
  assert(bus.writes[first + 1] == std::make_pair(uint8_t(1), uint8_t(0)));
  assert(bus.writes.back() == std::make_pair(uint8_t(0x0c), uint8_t(1)));
  assert(bus.regs[1] == 0 && bus.regs[0x0c] == 1);
  assert(goCount(bus) == 1);

  bool playing = false;
  assert(driver.readPlaying(playing) && playing);
  assert(driver.readPlaying(playing) && playing);
  assert(goCount(bus) == 1); // Polls cannot replay an active batch.
  assert(!driver.startSixPulse()); // Nor can a duplicate start restart it.
  assert(!driver.startConfirmation()); // A confirmation cannot interrupt it.
  assert(goCount(bus) == 1);

  bus.regs[0x0c] = 0; // Hardware clears GO after the first finite batch.
  assert(driver.readPlaying(playing) && playing);
  assert(bus.regs[0x0c] == 1 && goCount(bus) == 2);
  countBatch(bus, false, pulses, waits);
  assert(pulses == 6 && waits == 5);
  assert(driver.readPlaying(playing) && playing);
  assert(goCount(bus) == 2);

  bus.regs[0x0c] = 0; // The second completion ends the entire alert.
  assert(driver.readPlaying(playing) && !playing);
  assert(driver.readPlaying(playing) && !playing);
  assert(goCount(bus) == 2);
  assert(driver.standby());
  assert(bus.regs[1] == 0x40 && bus.regs[0x0c] == 0);
  assert(bus.timeout == 173);
  const auto count = bus.writes.size();
  driver.end();
  assert(bus.writes.size() == count);
  assert(!driver.available() && !driver.startSixPulse());
  assert(!driver.startConfirmation());
}

void confirmationPlaysOneFiniteBuzzWithoutQueuedTimerBatches() {
  watch::WatchHaptics driver;
  TwoWire bus;
  assert(driver.begin(bus));
  const auto first = bus.writes.size();
  assert(driver.startConfirmation());
  assert(bus.writes[first] == std::make_pair(uint8_t(0x0c), uint8_t(0)));
  assert(bus.writes[first + 1] == std::make_pair(uint8_t(1), uint8_t(0)));
  assert(bus.regs[4] == 15); // Finite 750 ms hardware safety limit; main caps at 500 ms.
  for (unsigned slot = 1; slot < 8; ++slot) assert(bus.regs[4 + slot] == 0);
  assert(bus.writes.back() == std::make_pair(uint8_t(0x0c), uint8_t(1)));
  assert(goCount(bus) == 1 && bus.regs[1] == 0);
  assert(!driver.startConfirmation() && !driver.startSixPulse());
  bool playing = false;
  assert(driver.readPlaying(playing) && playing);
  assert(driver.readPlaying(playing) && playing);
  assert(goCount(bus) == 1);
  bus.regs[0x0c] = 0;
  assert(driver.readPlaying(playing) && !playing);
  assert(driver.readPlaying(playing) && !playing);
  assert(goCount(bus) == 1); // Completion never starts timer pulses or another buzz.
  assert(bus.timeout == 173);
}

void confirmationStopAndReinitializationCannotReplayIt() {
  watch::WatchHaptics driver;
  TwoWire bus;
  assert(driver.begin(bus) && driver.startSixPulse());
  assert(driver.standby()); // Discard the timer's queued second batch.
  assert(driver.startConfirmation());
  bool playing = true;
  bus.regs[0x0c] = 0;
  assert(driver.readPlaying(playing) && !playing);
  assert(goCount(bus) == 2);
  assert(driver.startConfirmation() && driver.standby());
  assert(driver.readPlaying(playing) && !playing);
  assert(goCount(bus) == 3);
  assert(driver.startConfirmation());
  driver.end();
  assert(!driver.readPlaying(playing) && !playing);
  assert(!driver.startConfirmation());
  assert(driver.begin(bus));
  assert(driver.readPlaying(playing) && !playing);
  assert(goCount(bus) == 4);
  assert(driver.startSixPulse()); // Confirmation leaves no state that blocks a timer.
  bus.regs[0x0c] = 0;
  assert(driver.readPlaying(playing) && playing);
  assert(goCount(bus) == 6); // Exactly the timer's two explicit batches.
  assert(bus.timeout == 173);
}

void confirmationFaultsAndPartialWritesFailSafely() {
  watch::WatchHaptics driver;
  TwoWire bus;
  assert(driver.begin(bus));
  bus.regs[0] = 0x61;
  assert(!driver.startConfirmation() && !driver.available());
  assert(bus.regs[1] == 0x40 && bus.regs[0x0c] == 0);
  assert(goCount(bus) == 0);
  bus.regs[0] = 0x60;
  assert(driver.begin(bus));
  bus.fail_reg = 6;
  assert(!driver.startConfirmation() && !driver.available());
  assert(bus.regs[1] == 0x40 && bus.regs[0x0c] == 0);
  assert(goCount(bus) == 0);
  bus.fail_reg = -1;
  assert(driver.begin(bus) && driver.startConfirmation());
  bool playing = true;
  bus.fail_reads = true;
  assert(!driver.readPlaying(playing) && playing);
  assert(!driver.available());
  assert(!driver.startConfirmation() && !driver.startSixPulse());
  assert(goCount(bus) == 1 && bus.timeout == 173);
}

void invalidDevicesAndFaultsCannotStartPlayback() {
  for (uint8_t status : {uint8_t(0), uint8_t(0xff), uint8_t(0x61), uint8_t(0x62)}) {
    watch::WatchHaptics driver;
    TwoWire bus;
    bus.regs[0] = status;
    assert(!driver.begin(bus));
    assert(bus.writes.empty() && bus.timeout == 173);
  }
  watch::WatchHaptics driver;
  TwoWire bus;
  bus.regs[0] = 0xe0; // The compatible DRV2605L is accepted too.
  assert(driver.begin(bus));
  assert(driver.startSixPulse());
  assert(driver.standby());
}

void unavailableBusAndPartialConfigurationFailSafely() {
  watch::WatchHaptics driver;
  TwoWire bus;
  bus.fail_reads = true;
  assert(!driver.begin(bus));
  assert(bus.writes.empty() && bus.timeout == 173);
  bus.fail_reads = false;
  bus.fail_reg = 0x17;
  assert(!driver.begin(bus) && !driver.available());
  assert(bus.regs[1] == 0x40 && bus.regs[0x0c] == 0);
  assert(bus.timeout == 173);
}

void aNewFaultOrPartialSequenceCancelsPlayback() {
  watch::WatchHaptics driver;
  TwoWire bus;
  assert(driver.begin(bus));
  bus.regs[0] = 0x62;
  assert(!driver.startSixPulse() && !driver.available());
  assert(bus.regs[1] == 0x40 && bus.regs[0x0c] == 0);
  bus.regs[0] = 0x60;
  assert(driver.begin(bus));
  bus.fail_reg = 6;
  assert(!driver.startSixPulse() && !driver.available());
  assert(bus.regs[1] == 0x40 && bus.regs[0x0c] == 0);
}

void failedStopStillAttemptsStandbyAndInvalidatesDriver() {
  watch::WatchHaptics driver;
  TwoWire bus;
  assert(driver.begin(bus) && driver.startSixPulse());
  bus.fail_reg = 0x0c;
  assert(!driver.standby() && !driver.available());
  assert(bus.regs[1] == 0x40 && bus.timeout == 173);
}

void readErrorsAreNotReportedAsCompletion() {
  watch::WatchHaptics driver;
  TwoWire bus;
  assert(driver.begin(bus) && driver.startSixPulse());
  bool playing = true;
  bus.fail_reads = true;
  assert(!driver.readPlaying(playing) && playing);
  assert(bus.timeout == 173);
}

void standbyCancelsTheQueuedSecondBatch() {
  watch::WatchHaptics driver;
  TwoWire bus;
  assert(driver.begin(bus) && driver.startSixPulse());
  assert(driver.standby());
  bool playing = true;
  assert(driver.readPlaying(playing) && !playing);
  assert(driver.readPlaying(playing) && !playing);
  assert(goCount(bus) == 1);
  assert(driver.startSixPulse()); // An explicit new alert can start afresh.
  assert(goCount(bus) == 2);
}

void secondBatchFailureCannotProduceFurtherPulses() {
  watch::WatchHaptics driver;
  TwoWire bus;
  assert(driver.begin(bus) && driver.startSixPulse());
  bus.regs[0x0c] = 0;
  bus.fail_reg = 6;
  bool playing = true;
  assert(!driver.readPlaying(playing) && playing);
  assert(!driver.available());
  assert(bus.regs[1] == 0x40 && bus.regs[0x0c] == 0);
  assert(goCount(bus) == 1);
  bus.fail_reg = -1;
  assert(!driver.readPlaying(playing));
  assert(!driver.startSixPulse());
  assert(goCount(bus) == 1);
}

void endingOrReinitializingCannotResumeAnOldBatch() {
  watch::WatchHaptics driver;
  TwoWire bus;
  assert(driver.begin(bus) && driver.startSixPulse());
  driver.end();
  bool playing = true;
  bus.regs[0x0c] = 0;
  assert(!driver.readPlaying(playing) && playing);
  assert(driver.begin(bus));
  assert(driver.readPlaying(playing) && !playing);
  assert(goCount(bus) == 1);
  assert(driver.startSixPulse());
  bus.regs[0x0c] = 0;
  assert(driver.readPlaying(playing) && playing);
  assert(goCount(bus) == 3); // New first batch and its one second batch.
}

}  // namespace

int main() {
  unavailableDriverDoesNotUseTheBus();
  initializationNeverStartsTheMotor();
  hardwareSequenceHasExactlySixClicksAndStops();
  confirmationPlaysOneFiniteBuzzWithoutQueuedTimerBatches();
  confirmationStopAndReinitializationCannotReplayIt();
  confirmationFaultsAndPartialWritesFailSafely();
  invalidDevicesAndFaultsCannotStartPlayback();
  unavailableBusAndPartialConfigurationFailSafely();
  aNewFaultOrPartialSequenceCancelsPlayback();
  failedStopStillAttemptsStandbyAndInvalidatesDriver();
  readErrorsAreNotReportedAsCompletion();
  standbyCancelsTheQueuedSecondBatch();
  secondBatchFailureCannotProduceFurtherPulses();
  endingOrReinitializingCannotResumeAnOldBatch();
  std::puts("Watch haptics: all 14 driver scenarios passed.");
}
