#include "Lsm6dslStartup.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace {

static constexpr uint8_t kWakeThreshold = 0x0a;
static constexpr uint8_t kCheckedRegisters[] = {
    0x10, 0x11, 0x12, 0x15, 0x18, 0x19, 0x0d,
    0x0e, 0x58, 0x5b, 0x5c, 0x5e, 0x5f};
static constexpr std::size_t kCheckCount = sizeof(kCheckedRegisters);

struct Sensor {
  std::array<uint8_t, 256> registers{};
  std::size_t reads = 0;
  std::size_t fail_at = std::numeric_limits<std::size_t>::max();

  void running(uint8_t threshold = kWakeThreshold) {
    registers[0x10] = 0x40;
    registers[0x11] = 0x00;
    registers[0x12] = 0x44;
    registers[0x15] = 0x00;
    registers[0x18] = 0x00;
    registers[0x19] = 0x14;
    registers[0x0d] = 0x00;
    registers[0x0e] = 0x00;
    registers[0x58] = 0x81;
    registers[0x5b] = threshold;
    registers[0x5c] = 0x00;
    registers[0x5e] = 0x20;
    registers[0x5f] = 0x20;
    registers[0x4b] = 0xff;
    registers[0x4c] = 0xff;
  }

  bool probe(uint8_t threshold = kWakeThreshold) {
    return watch::lsm6dslRunning(
        [this](uint8_t reg, uint8_t& value) {
          // Configuration validation must not consume a live counter sample
          // or clear an interrupt source as a side effect.
          bool checked = false;
          for (uint8_t address : kCheckedRegisters) checked |= address == reg;
          assert(checked);
          value = registers[reg];
          return reads++ != fail_at;
        }, threshold);
  }
};

void runningConfigurationIsRecognizedWithoutMutation() {
  Sensor sensor;
  sensor.running();
  const auto before = sensor.registers;
  assert(sensor.probe());
  assert(sensor.reads == kCheckCount);
  assert(sensor.registers == before);
}

void powerUpDefaultsCannotClaimCounterContinuity() {
  Sensor sensor;
  sensor.registers[0x12] = 0x04; // IF_INC defaults on after power-up.
  assert(!sensor.probe());
  assert(sensor.reads == 1);
}

void everyFailedReadRejectsEvenWhenItReturnsTheExpectedValue() {
  for (std::size_t failure = 0; failure < kCheckCount; ++failure) {
    Sensor sensor;
    sensor.running();
    sensor.fail_at = failure;
    assert(!sensor.probe());
    assert(sensor.reads == failure + 1); // Stop at the first uncertain read.
  }
}

void everyConfigurationMismatchRejectsWarmReuse() {
  for (uint8_t reg : kCheckedRegisters) {
    Sensor sensor;
    sensor.running();
    sensor.registers[reg] ^= 0x01;
    assert(!sensor.probe());
    assert(sensor.reads <= kCheckCount);
  }
}

void resetOrRebootInProgressIsNeverAccepted() {
  for (uint8_t command : {uint8_t(0x01), uint8_t(0x80)}) {
    Sensor sensor;
    sensor.running();
    sensor.registers[0x12] |= command;
    assert(!sensor.probe());
  }
  Sensor counter_reset;
  counter_reset.running();
  counter_reset.registers[0x19] |= 0x02;
  assert(!counter_reset.probe());
}

void expectedWakeThresholdComesFromTheCaller() {
  Sensor sensor;
  sensor.running(0x0b);
  assert(!sensor.probe(kWakeThreshold));
  sensor.reads = 0;
  assert(sensor.probe(0x0b));
  assert(sensor.reads == kCheckCount);
}

} // namespace

int main() {
  runningConfigurationIsRecognizedWithoutMutation();
  powerUpDefaultsCannotClaimCounterContinuity();
  everyFailedReadRejectsEvenWhenItReturnsTheExpectedValue();
  everyConfigurationMismatchRejectsWarmReuse();
  resetOrRebootInProgressIsNeverAccepted();
  expectedWakeThresholdComesFromTheCaller();
  std::puts("LSM6DSL startup: all 6 scenarios passed (13 read failures and mismatches covered).");
}
