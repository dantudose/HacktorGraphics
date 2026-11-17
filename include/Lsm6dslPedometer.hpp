#pragma once

#include <cstdint>

namespace watch {

// Configure a genuine LSM6DSL after its software reset has completed, with
// both accelerometer and gyroscope still powered down. Bank-A writes require
// that state; the caller starts the accelerometer after this function returns.
// Read/write callbacks operate on the same device and bound their I2C waits.
// LSM6DSL DS11450: FUNC_CFG_ACCESS, CTRL10_C, CONFIG_PEDO_THS_MIN, PEDO_DEB_REG.
template <class Read, class Write>
bool configureLsm6dslPedometer(Read read, Write write) {
  bool configured = write(0x01, 0x80); // FUNC_CFG_ACCESS: bank A.
  if (configured) {
    uint8_t threshold = 0;
    uint8_t debounce = 0;
    // Explicitly restore ST's 2 g threshold and debounce defaults. Neither
    // these settings nor CTRL10_C assert the hardware step-counter reset.
    configured = write(0x0f, 0x10) && // CONFIG_PEDO_THS_MIN.
                 write(0x14, 0x6e) && // PEDO_DEB_REG.
                 read(0x0f, threshold) && threshold == 0x10 &&
                 read(0x14, debounce) && debounce == 0x6e;
  }

  // A failed write may still reach the device. Always leave bank A, even if
  // entering it failed. Retry restoration once, but any first failure still
  // makes this initialization fail instead of enabling an uncertain setup.
  const bool restored = write(0x01, 0x00);
  if (!restored) {
    write(0x01, 0x00);
    return false;
  }
  if (!configured) return false;

  uint8_t control = 0;
  if (write(0x19, 0x14) && // CTRL10_C: PEDO_EN | FUNC_EN only.
      read(0x19, control) && control == 0x14) return true;

  // A NACK or a failed readback after the enable write is ambiguous. Disable
  // the functions again, with one bounded retry, and report failure. A bus
  // that remains unavailable cannot guarantee cleanup; callers must not mark
  // the pedometer ready on a false return.
  if (!write(0x19, 0x00)) write(0x19, 0x00);
  return false;
}

} // namespace watch
