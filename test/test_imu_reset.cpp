#include "ImuReset.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

namespace {

struct Sample {
  bool success;
  uint8_t control;
  uint32_t duration_ms;
};

struct Sensor {
  uint32_t clock_ms = 0;
  uint32_t paused_ms = 0;
  unsigned reads = 0;
  unsigned pauses = 0;
  std::vector<Sample> samples;
  Sample fallback{false, 0, 0};

  bool wait(uint32_t timeout_ms = 100) {
    return watch::waitForImuReset(
        [this](uint8_t reg, uint8_t& control) {
          assert(reg == 0x12);
          const Sample sample = reads < samples.size() ? samples[reads] : fallback;
          ++reads;
          clock_ms += sample.duration_ms;
          // Even failed reads can leave zero in a caller's output buffer.
          // Success must be conditional on the callback's result.
          control = sample.control;
          return sample.success;
        },
        [this](uint32_t duration_ms) {
          assert(duration_ms == 1);
          ++pauses;
          paused_ms += duration_ms;
          clock_ms += duration_ms;
        },
        [this]() { return clock_ms; }, timeout_ms);
  }
};

void transientNackAndBusyReadRecover() {
  Sensor sensor;
  sensor.samples = {{false, 0, 2}, {true, 0x05, 1}, {true, 0x44, 1}};
  assert(sensor.wait());
  assert(sensor.reads == 3 && sensor.pauses == 3 && sensor.clock_ms == 7);
}

void readySensorNeedsOneConfirmedRead() {
  Sensor sensor;
  sensor.samples = {{true, 0x04, 0}};
  assert(sensor.wait());
  assert(sensor.reads == 1 && sensor.paused_ms == 1);
}

void allNacksExpireWithoutFalseReady() {
  Sensor sensor;
  sensor.fallback = {false, 0, 0};
  assert(!sensor.wait(5));
  assert(sensor.clock_ms == 5 && sensor.reads == 4 && sensor.pauses == 5);
}

void stuckResetBitIsBounded() {
  Sensor sensor;
  sensor.fallback = {true, 0x45, 0};
  assert(!sensor.wait(5));
  assert(sensor.clock_ms == 5 && sensor.reads == 4 && sensor.pauses == 5);
}

void millisecondRolloverCanRecover() {
  Sensor sensor;
  sensor.clock_ms = std::numeric_limits<uint32_t>::max() - 1;
  sensor.samples = {{false, 0, 0}, {true, 0x01, 0}, {true, 0x00, 0}};
  assert(sensor.wait(5));
  assert(sensor.clock_ms == 1 && sensor.reads == 3 && sensor.pauses == 3);
}

void millisecondRolloverStillExpires() {
  Sensor sensor;
  sensor.clock_ms = std::numeric_limits<uint32_t>::max() - 1;
  assert(!sensor.wait(5));
  assert(sensor.clock_ms == 3 && sensor.reads == 4 && sensor.pauses == 5);
}

void readFinishingAtDeadlineDoesNotSucceed() {
  Sensor sensor;
  sensor.samples = {{true, 0x00, 4}};
  assert(!sensor.wait(5));
  assert(sensor.clock_ms == 5 && sensor.reads == 1 && sensor.pauses == 1);
}

void readFinishingAfterDeadlineDoesNotSucceedOrRetry() {
  Sensor sensor;
  sensor.samples = {{true, 0x00, 6}};
  assert(!sensor.wait(5));
  assert(sensor.clock_ms == 7 && sensor.reads == 1 && sensor.pauses == 1);
}

void failedTransferTimeCountsTowardDeadline() {
  Sensor sensor;
  sensor.samples = {{false, 0x00, 4}, {true, 0x00, 0}};
  assert(!sensor.wait(5));
  assert(sensor.clock_ms == 5 && sensor.reads == 1 && sensor.pauses == 1);
}

void zeroTimeoutPerformsNoTransferOrPause() {
  Sensor sensor;
  sensor.samples = {{true, 0x00, 0}};
  assert(!sensor.wait(0));
  assert(sensor.clock_ms == 0 && sensor.reads == 0 && sensor.pauses == 0);
}

void settlingPauseThatReachesDeadlineDoesNotRead() {
  Sensor sensor;
  sensor.samples = {{true, 0x00, 0}};
  assert(!sensor.wait(1));
  assert(sensor.clock_ms == 1 && sensor.reads == 0 && sensor.pauses == 1);
}

void lastReadBeforeDeadlineCanSucceed() {
  Sensor sensor;
  sensor.samples = {{false, 0x00, 0}, {true, 0x00, 2}};
  assert(sensor.wait(5));
  assert(sensor.clock_ms == 4 && sensor.reads == 2 && sensor.pauses == 2);
}

} // namespace

int main() {
  transientNackAndBusyReadRecover();
  readySensorNeedsOneConfirmedRead();
  allNacksExpireWithoutFalseReady();
  stuckResetBitIsBounded();
  millisecondRolloverCanRecover();
  millisecondRolloverStillExpires();
  readFinishingAtDeadlineDoesNotSucceed();
  readFinishingAfterDeadlineDoesNotSucceedOrRetry();
  failedTransferTimeCountsTowardDeadline();
  zeroTimeoutPerformsNoTransferOrPause();
  settlingPauseThatReachesDeadlineDoesNotRead();
  lastReadBeforeDeadlineCanSucceed();
  std::puts("IMU reset wait: all 12 scenarios passed.");
}
