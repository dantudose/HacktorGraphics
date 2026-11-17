#pragma once

#include <cstddef>
#include <cstdint>
#include <Wire.h>

namespace watch {
namespace step_counter_detail {

inline bool readRegisters(TwoWire& wire, uint8_t address, uint8_t reg,
                          uint8_t* bytes, std::size_t length) {
  wire.beginTransmission(address);
  const bool written = wire.write(reg) == 1;
  const bool sent = wire.endTransmission(!written) == 0;
  bool ok = written && sent &&
      wire.requestFrom(address, length, true) == length;
  if (ok) {
    for (std::size_t i = 0; i < length; ++i) {
      const int value = wire.read();
      if (value < 0) {
        ok = false;
        break;
      }
      bytes[i] = static_cast<uint8_t>(value);
    }
  }
  while (wire.available()) wire.read();
  return ok;
}

}  // namespace step_counter_detail

// Read the LSM6DSL's free-running 16-bit counter with IF_INC already enabled.
// A high-byte snapshot before the low/high burst catches a carry during that
// burst, including 0xffff -> 0. Retry once; never publish a torn or failed read.
inline bool readStepCounter(TwoWire& wire, uint8_t address, uint16_t& steps) {
  const uint16_t previous_timeout = wire.getTimeOut();
  wire.setTimeOut(10);
  bool ok = false;
  uint16_t candidate = 0;
  for (unsigned attempt = 0; attempt < 2; ++attempt) {
    uint8_t high_before = 0;
    uint8_t bytes[2] = {};
    if (!step_counter_detail::readRegisters(wire, address, 0x4c,
                                            &high_before, 1) ||
        !step_counter_detail::readRegisters(wire, address, 0x4b,
                                            bytes, sizeof(bytes))) break;
    if (high_before == bytes[1]) {
      candidate = static_cast<uint16_t>((uint16_t(bytes[1]) << 8) | bytes[0]);
      ok = true;
      break;
    }
  }
  wire.setTimeOut(previous_timeout);
  if (ok) steps = candidate;
  return ok;
}

}  // namespace watch
