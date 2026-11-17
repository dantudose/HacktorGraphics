#include "StepCounterRead.hpp"

#include <cassert>
#include <cstdio>

namespace {

void assertRestored(const TwoWire& wire) {
  assert(wire.timeout == 173);
  assert(wire.timeouts.size() == 2);
  assert(wire.timeouts[0] == 10 && wire.timeouts[1] == 173);
  assert(wire.available() == 0);
}

void ordinaryBurstIsPublished() {
  TwoWire wire;
  wire.transfers = {{0x4c, {0x12}}, {0x4b, {0x34, 0x12}}};
  uint16_t value = 999;
  assert(watch::readStepCounter(wire, 0x6b, value));
  assert(value == 0x1234 && wire.used == 2);
  assert(wire.addresses[0] == 0x6b && wire.addresses[1] == 0x6b);
  assertRestored(wire);
}

void lowByteCarryRetriesInsteadOfPublishingTornValue() {
  TwoWire wire;
  wire.transfers = {{0x4c, {0x00}}, {0x4b, {0xff, 0x01}},
                    {0x4c, {0x01}}, {0x4b, {0x00, 0x01}}};
  uint16_t value = 999;
  assert(watch::readStepCounter(wire, 0x6a, value));
  assert(value == 0x0100 && wire.used == 4);
  assertRestored(wire);
}

void fullCounterWrapRetries() {
  TwoWire wire;
  wire.transfers = {{0x4c, {0xff}}, {0x4b, {0xff, 0x00}},
                    {0x4c, {0x00}}, {0x4b, {0x01, 0x00}}};
  uint16_t value = 999;
  assert(watch::readStepCounter(wire, 0x6a, value));
  assert(value == 1 && wire.used == 4);
  assertRestored(wire);
}

void twoInconsistentAttemptsLeaveOutputUntouched() {
  TwoWire wire;
  wire.transfers = {{0x4c, {0x00}}, {0x4b, {0xff, 0x01}},
                    {0x4c, {0x01}}, {0x4b, {0xff, 0x02}}};
  uint16_t value = 999;
  assert(!watch::readStepCounter(wire, 0x6a, value));
  assert(value == 999 && wire.used == 4);
  assertRestored(wire);
}

void failedWriteStopsWithoutRequest() {
  TwoWire wire;
  wire.transfers = {{0x4c, {0x12}}};
  wire.transfers[0].write_ok = false;
  uint16_t value = 999;
  assert(!watch::readStepCounter(wire, 0x6a, value));
  assert(value == 999 && wire.request_count == 0 && wire.used == 1);
  assert(wire.ended_with_stop);
  assertRestored(wire);
}

void nackStopsWithoutRequest() {
  TwoWire wire;
  wire.transfers = {{0x4c, {0x12}}};
  wire.transfers[0].status = 4;
  uint16_t value = 999;
  assert(!watch::readStepCounter(wire, 0x6a, value));
  assert(value == 999 && wire.request_count == 0 && wire.used == 1);
  assertRestored(wire);
}

void shortBurstIsDrainedAndRejected() {
  TwoWire wire;
  wire.transfers = {{0x4c, {0x12}}, {0x4b, {0x34}}};
  uint16_t value = 999;
  assert(!watch::readStepCounter(wire, 0x6a, value));
  assert(value == 999 && wire.used == 2);
  assertRestored(wire);
}

void negativeReadIsDrainedAndRejected() {
  TwoWire wire;
  wire.transfers = {{0x4c, {0x12}}, {0x4b, {-1, 0x12}}};
  uint16_t value = 999;
  assert(!watch::readStepCounter(wire, 0x6a, value));
  assert(value == 999 && wire.used == 2);
  assertRestored(wire);
}

void highByteTransportFailureDoesNotRetry() {
  TwoWire wire;
  wire.transfers = {{0x4c, {}}};
  uint16_t value = 999;
  assert(!watch::readStepCounter(wire, 0x6a, value));
  assert(value == 999 && wire.used == 1);
  assertRestored(wire);
}

}  // namespace

int main() {
  ordinaryBurstIsPublished();
  lowByteCarryRetriesInsteadOfPublishingTornValue();
  fullCounterWrapRetries();
  twoInconsistentAttemptsLeaveOutputUntouched();
  failedWriteStopsWithoutRequest();
  nackStopsWithoutRequest();
  shortBurstIsDrainedAndRejected();
  negativeReadIsDrainedAndRejected();
  highByteTransportFailureDoesNotRetry();
  std::puts("Step counter read: all 9 scenarios passed.");
}
