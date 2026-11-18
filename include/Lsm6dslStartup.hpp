#pragma once

#include <cstdint>

namespace watch {

// Recognize the configuration left by this firmware before touching the live
// step counter or performing a cold initialization. The caller must first
// restore FUNC_CFG_ACCESS to the user bank and confirm WHO_AM_I == 0x6a.
// Read is bool(uint8_t register, uint8_t& value), with bounded I2C waits.
// This function only reads; it neither resets nor reconfigures the sensor.
template <typename Read>
bool lsm6dslRunning(Read read, uint8_t wake_threshold) {
  struct Register {
    uint8_t address;
    uint8_t expected;
  };
  const Register registers[] = {
      {0x10, 0x40}, // CTRL1_XL: 104 Hz, 2 g.
      {0x11, 0x00}, // CTRL2_G: gyroscope powered down.
      {0x12, 0x44}, // CTRL3_C: BDU/IF_INC, no BOOT or SW_RESET.
      {0x15, 0x00}, // CTRL6_C: accelerometer high performance.
      {0x18, 0x00}, // CTRL9_XL: DEN/soft-iron disabled.
      {0x19, 0x14}, // CTRL10_C: FUNC_EN/PEDO_EN, no PEDO_RST_STEP.
      {0x0d, 0x00}, // INT1_CTRL: no step/data-ready interrupts.
      {0x0e, 0x00}, // INT2_CTRL: no step/data-ready interrupts.
      {0x58, 0x81}, // TAP_CFG: interrupt enable and latched wake.
      {0x5b, wake_threshold}, // WAKE_UP_THS: firmware's configured threshold.
      {0x5c, 0x00}, // WAKE_UP_DUR: no additional duration requirement.
      {0x5e, 0x20}, // MD1_CFG: wake-up routed to INT1.
      {0x5f, 0x20}, // MD2_CFG: wake-up routed to INT2.
  };
  for (const Register& reg : registers) {
    uint8_t value = 0;
    if (!read(reg.address, value) || value != reg.expected) return false;
  }
  return true;
}

} // namespace watch
