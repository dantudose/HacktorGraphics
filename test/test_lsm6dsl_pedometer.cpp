// Native: c++ -std=c++11 -Wall -Wextra -Werror -Iinclude
//         test/test_lsm6dsl_pedometer.cpp -o /tmp/hacktor-lsm6dsl-tests
#include "Lsm6dslPedometer.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

struct Operation {
  bool read;
  uint8_t bank;
  uint8_t reg;
  uint8_t value;
};

// Register banks are separate: a missed bank restore must not accidentally
// look like a successful user-register read or write. Failures can happen
// before or after an acknowledged-looking transfer reaches the sensor.
struct Sensor {
  std::array<uint8_t, 256> user{};
  std::array<uint8_t, 256> embedded{};
  std::vector<Operation> operations;
  std::vector<std::size_t> fail_at;
  std::vector<std::size_t> corrupt_read_at;
  std::vector<std::size_t> drop_write_at;
  uint8_t bank = 0;
  unsigned enable_writes = 0;
  bool apply_failed_writes = false;

  Sensor() {
    // The startup caller has completed SW_RESET, leaving both sensors off.
    user[0x10] = 0;
    user[0x11] = 0;
    user[0x19] = 0;
    embedded[0x0f] = 0x9f; // Deliberately stale threshold/debounce settings.
    embedded[0x14] = 0xff;
    embedded[0x04] = 0xa4;
    embedded[0x05] = 0xa5;
    user[0x4b] = 0xe1;
    user[0x4c] = 0x7b;
  }

  static bool contains(const std::vector<std::size_t>& indices, std::size_t index) {
    for (const auto value : indices) if (value == index) return true;
    return false;
  }

  void assertPoweredDown() const {
    assert((user[0x10] & 0xf0) == 0);
    assert((user[0x11] & 0xf0) == 0);
  }

  bool write(uint8_t reg, uint8_t value) {
    const auto index = operations.size();
    operations.push_back({false, bank, reg, value});
    assert(reg != 0x04 && reg != 0x05); // Not LSM6DSO enable registers.
    assert(reg != 0x10 && reg != 0x11); // The caller owns ODR/power setup.
    const bool failed = contains(fail_at, index);
    if ((failed && !apply_failed_writes) || contains(drop_write_at, index)) {
      return !failed;
    }
    if (reg == 0x01) {
      assert(value == 0 || value == 0x80);
      bank = value;
    } else if (bank == 0x80) {
      assertPoweredDown();
      assert(reg == 0x0f || reg == 0x14);
      embedded[reg] = value;
    } else {
      assert(reg == 0x19 && (value == 0 || value == 0x14));
      assert((value & 0x02) == 0); // Never reset the free-running step count.
      if (value == 0x14) {
        assertPoweredDown();
        assert(embedded[0x0f] == 0x10 && embedded[0x14] == 0x6e);
        ++enable_writes;
      }
      user[reg] = value;
    }
    return !failed;
  }

  bool read(uint8_t reg, uint8_t& value) {
    const auto index = operations.size();
    operations.push_back({true, bank, reg, 0});
    if (contains(fail_at, index)) return false;
    if (bank == 0x80) {
      assertPoweredDown();
      assert(reg == 0x0f || reg == 0x14);
      value = embedded[reg];
    } else {
      assert(reg == 0x19);
      value = user[reg];
    }
    if (contains(corrupt_read_at, index)) value ^= 0x01;
    return true;
  }

  bool configure() {
    return watch::configureLsm6dslPedometer(
        [this](uint8_t reg, uint8_t& value) { return read(reg, value); },
        [this](uint8_t reg, uint8_t value) { return write(reg, value); });
  }

  void assertUntouchedRegisters() const {
    assert(embedded[0x04] == 0xa4 && embedded[0x05] == 0xa5);
    assert(user[0x4b] == 0xe1 && user[0x4c] == 0x7b);
    assertPoweredDown();
  }
};

void defaultsAreVerifiedBeforeMinimalEnable() {
  Sensor sensor;
  assert(sensor.configure());
  assert(sensor.bank == 0 && sensor.user[0x19] == 0x14);
  assert(sensor.enable_writes == 1);
  assert(sensor.operations.size() == 8);
  const Operation expected[] = {
      {false, 0, 0x01, 0x80}, {false, 0x80, 0x0f, 0x10},
      {false, 0x80, 0x14, 0x6e}, {true, 0x80, 0x0f, 0},
      {true, 0x80, 0x14, 0}, {false, 0x80, 0x01, 0},
      {false, 0, 0x19, 0x14}, {true, 0, 0x19, 0}};
  for (std::size_t i = 0; i < sensor.operations.size(); ++i) {
    const auto& actual = sensor.operations[i];
    assert(actual.read == expected[i].read && actual.bank == expected[i].bank &&
           actual.reg == expected[i].reg && actual.value == expected[i].value);
  }
  sensor.assertUntouchedRegisters();
}

void everyTransferFailureLeavesPedometerUnready() {
  // Cover every read and write, including bank entry, both configuration
  // readbacks, bank restore, enable, and final enable verification.
  for (std::size_t failure = 0; failure < 8; ++failure) {
    for (bool applied : {false, true}) {
      Sensor sensor;
      sensor.fail_at = {failure};
      sensor.apply_failed_writes = applied;
      assert(!sensor.configure());
      assert(sensor.bank == 0 && sensor.user[0x19] == 0);
      if (failure < 6) assert(sensor.enable_writes == 0);
      sensor.assertUntouchedRegisters();
    }
  }
}

void readbackMismatchCannotClaimSuccessfulSetup() {
  for (std::size_t mismatch : {std::size_t(3), std::size_t(4), std::size_t(7)}) {
    Sensor sensor;
    sensor.corrupt_read_at = {mismatch};
    assert(!sensor.configure());
    assert(sensor.bank == 0 && sensor.user[0x19] == 0);
    if (mismatch < 6) assert(sensor.enable_writes == 0);
    sensor.assertUntouchedRegisters();
  }
}

void acknowledgedButIgnoredSettingsAreRejectedByReadback() {
  for (std::size_t ignored : {std::size_t(1), std::size_t(2), std::size_t(6)}) {
    Sensor sensor;
    sensor.drop_write_at = {ignored};
    assert(!sensor.configure());
    assert(sensor.bank == 0 && sensor.user[0x19] == 0);
    if (ignored < 6) assert(sensor.enable_writes == 0);
    sensor.assertUntouchedRegisters();
  }
}

void failedBankRestoreHasOneRetryAndNeverEnables() {
  Sensor sensor;
  sensor.fail_at = {5};
  assert(!sensor.configure());
  assert(sensor.operations.size() == 7);
  assert(sensor.operations[5].reg == 0x01 && sensor.operations[5].value == 0);
  assert(sensor.operations[6].reg == 0x01 && sensor.operations[6].value == 0);
  assert(sensor.bank == 0 && sensor.enable_writes == 0);

  Sensor unavailable;
  unavailable.fail_at = {5, 6};
  assert(!unavailable.configure());
  assert(unavailable.operations.size() == 7);
  assert(unavailable.bank == 0x80 && unavailable.enable_writes == 0);
  assert(unavailable.user[0x19] == 0);
}

void failedEnableCleanupHasOneBoundedRetry() {
  Sensor sensor;
  sensor.fail_at = {7, 8}; // Final readback, then the first disable write.
  assert(!sensor.configure());
  assert(sensor.operations.size() == 10);
  assert(sensor.operations[8].reg == 0x19 && sensor.operations[8].value == 0);
  assert(sensor.operations[9].reg == 0x19 && sensor.operations[9].value == 0);
  assert(sensor.bank == 0 && sensor.user[0x19] == 0);

  Sensor unavailable;
  unavailable.fail_at = {7, 8, 9};
  assert(!unavailable.configure());
  assert(unavailable.operations.size() == 10);
  // An unavailable bus cannot guarantee cleanup; it must never report ready.
  assert(unavailable.user[0x19] == 0x14);
}

} // namespace

int main() {
  defaultsAreVerifiedBeforeMinimalEnable();
  everyTransferFailureLeavesPedometerUnready();
  readbackMismatchCannotClaimSuccessfulSetup();
  acknowledgedButIgnoredSettingsAreRejectedByReadback();
  failedBankRestoreHasOneRetryAndNeverEnables();
  failedEnableCleanupHasOneBoundedRetry();
  std::puts("LSM6DSL pedometer: all 6 scenarios passed (16 transfer failures covered).");
}
