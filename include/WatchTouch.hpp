#pragma once

#include <Arduino.h>
#include <Wire.h>

#include "WatchTouchData.hpp"

#ifndef HACKTOR_TOUCH_ROTATION
#define HACKTOR_TOUCH_ROTATION 2
#endif

namespace watch {

// Hacktor Watch 2.0 uses CST816T at 0x15, reset GPIO2, IRQ GPIO1.
// Share the existing Wire bus; never start/stop it or enable a touch wake IRQ.
// Main must reset LOW >= 30 ms, HIGH >= 50 ms before begin(), and call end()
// before parking I2C/cutting 3V3_2. Poll only while a watchface is lit.
// Protocol reference: SensorLib/src/touch/TouchDrvCST816.{hpp,cpp}.
class WatchTouch {
 public:
  bool begin(TwoWire& wire) {
    wire_ = &wire;
    initialized_ = false;
    failures_ = 0;
    retry_delay_ms_ = 0;
    last_read_had_error_ = false;
    // Default auto-sleep can stop I2C responses even with the display visible.
    // The whole touch supply is still switched off in the watch's standby.
    if (!writeRegister(0xfe, 0x01)) return false;
    // Firmware variants can omit a readable ID. The ACKed configuration write
    // at this board's known touch address is sufficient to enable polling;
    // every touch packet is still validated before it can produce an action.
    chip_id_ = 0;
    readRegisters(0xa7, &chip_id_, 1);
    initialized_ = true;
    return true;
  }

  void end() {
    initialized_ = false;
    wire_ = nullptr;
    failures_ = 0;
    retry_delay_ms_ = 0;
    last_read_had_error_ = false;
  }

  bool available() const { return initialized_; }
  uint8_t chipId() const { return chip_id_; }
  bool lastReadHadError() const { return last_read_had_error_; }

  // False means no fresh valid sample. Never turn that into a release/tap.
  // Cancel a pending gesture when lastReadHadError() is true. An ACKed invalid
  // or idle all-FF packet leaves it false: ignore that sample and retain the
  // previous contact until a valid Up/FingerNum0 packet arrives.
  bool read(TouchPoint& point) {
    last_read_had_error_ = false;
    if (!initialized_) {
      last_read_had_error_ = true;
      return false;
    }
    const uint32_t now = millis();
    if (retry_delay_ms_ && now - last_failure_ms_ < retry_delay_ms_) {
      last_read_had_error_ = true;
      return false;
    }
    uint8_t bytes[5];
    if (!readRegisters(0x02, bytes, sizeof(bytes))) {
      last_read_had_error_ = true;
      last_failure_ms_ = now;
      if (failures_ < 5) ++failures_;
      retry_delay_ms_ = failures_ >= 5 ? 1000 : (50U << (failures_ - 1));
      return false;
    }
    failures_ = 0;
    retry_delay_ms_ = 0;
    // An ACKed all-FF frame can be a CST816T idle firmware response. Reject it
    // without backing off: a subsequent short tap must still be polled quickly.
    return decodeCst816Point(bytes, sizeof(bytes), point, HACKTOR_TOUCH_ROTATION);
  }

 private:
  // Touch transactions are bounded without changing the timeout used by the
  // fuel gauge/IMU. All Wire activity is on the main loop, never in an ISR.
  bool readRegisters(uint8_t reg, uint8_t* bytes, std::size_t length) {
    if (!wire_) return false;
    const uint16_t previous_timeout = wire_->getTimeOut();
    wire_->setTimeOut(10);
    wire_->beginTransmission(0x15);
    const bool written = wire_->write(reg) == 1;
    const bool sent = wire_->endTransmission(false) == 0;
    bool ok = written && sent && wire_->requestFrom(uint8_t(0x15), length, true) == length;
    if (ok) {
      for (std::size_t i = 0; i < length; ++i) {
        const int value = wire_->read();
        if (value < 0) {
          ok = false;
          break;
        }
        bytes[i] = static_cast<uint8_t>(value);
      }
    }
    while (wire_->available()) wire_->read();
    wire_->setTimeOut(previous_timeout);
    return ok;
  }

  bool writeRegister(uint8_t reg, uint8_t value) {
    const uint16_t previous_timeout = wire_->getTimeOut();
    wire_->setTimeOut(10);
    wire_->beginTransmission(0x15);
    const bool written = wire_->write(reg) == 1 && wire_->write(value) == 1;
    const bool sent = wire_->endTransmission(true) == 0;
    wire_->setTimeOut(previous_timeout);
    return written && sent;
  }

  TwoWire* wire_ = nullptr;
  bool initialized_ = false;
  bool last_read_had_error_ = false;
  uint8_t chip_id_ = 0;
  uint8_t failures_ = 0;
  uint32_t last_failure_ms_ = 0;
  uint32_t retry_delay_ms_ = 0;
};

}  // namespace watch
