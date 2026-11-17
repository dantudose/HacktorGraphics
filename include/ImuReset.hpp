#pragma once

#include <cstdint>

namespace watch {

// Wait for CTRL3_C.SW_RESET to clear after a reset command. The IMU may not
// acknowledge I2C while resetting; only a successful read can confirm ready.
// Read callbacks must bound their own I2C wait. Millisecond time includes those
// transfers and the 1 ms settling/retry pauses, with unsigned rollover handling.
// A clear read that finishes at or after the deadline is still a timeout.
template <class Read, class Pause, class Now>
bool waitForImuReset(Read read, Pause pause, Now now, uint32_t timeout_ms = 100) {
  const uint32_t started_ms = now();
  while (static_cast<uint32_t>(now() - started_ms) < timeout_ms) {
    pause(1);
    if (static_cast<uint32_t>(now() - started_ms) >= timeout_ms) return false;

    uint8_t control = 0x01;
    const bool valid = read(0x12, control);
    if (static_cast<uint32_t>(now() - started_ms) >= timeout_ms) return false;
    if (valid && (control & 0x01) == 0) return true;
  }
  return false;
}

} // namespace watch
