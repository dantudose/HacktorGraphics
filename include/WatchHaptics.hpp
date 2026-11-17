#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <cstddef>
#include <cstdint>

namespace watch {

// DRV2605 / DRV2605L on the watch's shared I2C bus (0x5A).
// Main owns rail power and a bounded completion deadline. Call begin after
// power settles, standby before parking I2C, and end when supply is removed.
// The board's ERM has no published part/rating: use a conservative open-loop
// profile, not an unverified LRA frequency or a calibration vibration at boot.
// Protocol: TI DRV2605 SLOS825E, sections 7.5.2 and 7.5.8, registers 0x00..0x1D.
// https://www.ti.com/lit/ds/symlink/drv2605.pdf
class WatchHaptics {
 public:
  bool begin(TwoWire& wire) {
    wire_ = &wire;
    ready_ = false;
    alert_active_ = false;
    batches_left_ = 0;
    uint8_t status = 0;
    if (!readRegister(0x00, status)) return false;
    const uint8_t device_id = status >> 5;
    if ((device_id != 3 && device_id != 7) || (status & 0x03)) return false;

    // Stay in standby until an explicit playback request. The ROM library uses
    // signed, bidirectional input; unidirectional is not library-compatible.
    // In open loop, full-scale average ERM drive is OD_CLAMP * 21.96 mV
    // (DRV2605) / 21.59 mV (DRV2605L). 113 keeps either below 2.5 V.
    if (!writeRegister(0x01, 0x40) || // Standby, internal-trigger mode.
        !writeRegister(0x0c, 0x00) || // Cancel any old library/calibration GO.
        !writeRegister(0x02, 0x00) || // Zero any stale RTP value defensively.
        !writeRegister(0x03, 0x01) || // ERM library A, normal output impedance.
        !writeRegister(0x17, 113) ||  // Conservative ERM open-loop voltage.
        !writeRegister(0x1a, 0x36) || // ERM (bit7 clear), default feedback.
        !writeRegister(0x1b, 0x13) || // Disable startup boost; default drive time.
        !writeRegister(0x1c, 0xf5) || // Bidirectional input; default timing.
        !writeRegister(0x1d, 0xa0)) { // Open-loop ERM, signed RTP, supply comp.
      standby();
      return false;
    }
    ready_ = true;
    return true;
  }

  bool available() const { return ready_; }

  // One uninterrupted confirmation buzz. Main stops it after 500 ms; effect
  // 15 is TI's finite 750 ms alert (section 11.2), so even a delayed CPU or
  // failed stop cannot leave the motor running indefinitely. Unlike the timer
  // alert, there is no queued second batch and no software-driven repetition.
  bool startConfirmation() {
    if (!ready_ || alert_active_) return false;
    if (!preparePlayback()) return false;
    const uint8_t sequence[] = {15, 0, 0, 0, 0, 0, 0, 0};
    if (!startSequence(sequence)) {
      standby();
      ready_ = false;
      return false;
    }
    alert_active_ = true;
    batches_left_ = 0;
    return true;
  }

  // Six single clicks with 150 ms gaps require more than the eight hardware
  // slots. Play two finite three-click batches; readPlaying starts the second
  // only after the first clears GO. A final wait in batch one separates them.
  // Every batch stops autonomously, even while a display wake blocks the CPU.
  bool startSixPulse() {
    if (!ready_ || alert_active_) return false;
    if (!preparePlayback()) return false;
    // TI section 8.3.2.1: leave standby, load slots, then assert GO. Eight
    // bus writes give the ready state over 500 us at 400 kHz; main supplies
    // the separate 250 us power-up wait through its rail settling delay.
    if (!startBatch(true)) {
      standby();
      ready_ = false;
      return false;
    }
    alert_active_ = true;
    batches_left_ = 1;
    return true;
  }

  // False is a transport/device error, not normal completion. Output is left
  // unchanged on failure; main aborts and releases a temporary rail lease.
  bool readPlaying(bool& playing) {
    if (!ready_) return false;
    if (!alert_active_) {
      playing = false;
      return true;
    }
    uint8_t status = 0;
    uint8_t go = 0;
    if (!readRegister(0x00, status) || (status & 0x03) ||
        !readRegister(0x0c, go)) {
      batches_left_ = 0;
      alert_active_ = false;
      ready_ = false;
      return false;
    }
    if ((go & 0x01) == 0 && batches_left_) {
      batches_left_ = 0;
      if (!startBatch(false)) {
        standby();
        ready_ = false;
        return false;
      }
      playing = true;
      return true;
    }
    playing = (go & 0x01) != 0;
    if (!playing) alert_active_ = false;
    return true;
  }

  // Both writes are attempted: mode standby can still stop the motor if the
  // GO transfer failed. Hardware playback is also finite independently of I2C.
  bool standby() {
    batches_left_ = 0;
    alert_active_ = false;
    if (!wire_) return false;
    const bool cancelled = writeRegister(0x0c, 0x00);
    const bool stopped = writeRegister(0x01, 0x40);
    if (!cancelled || !stopped) ready_ = false;
    return cancelled && stopped;
  }

  // No bus access here: safe after the rail has already been turned off.
  void end() {
    ready_ = false;
    alert_active_ = false;
    batches_left_ = 0;
    wire_ = nullptr;
  }

 private:
  bool preparePlayback() {
    uint8_t status = 0;
    if (!readRegister(0x00, status) || (status & 0x03) ||
        !writeRegister(0x0c, 0x00) || !writeRegister(0x01, 0x00)) {
      standby();
      ready_ = false;
      return false;
    }
    return true;
  }

  bool startBatch(bool trailing_wait) {
    // WAIT=bit7, remaining bits=10 ms units; zero terminates playback.
    const uint8_t sequence[] = {1, 0x8f, 1, 0x8f, 1,
                               static_cast<uint8_t>(trailing_wait ? 0x8f : 0), 0, 0};
    return startSequence(sequence);
  }

  bool startSequence(const uint8_t (&sequence)[8]) {
    // Always load all eight slots before GO. Besides replacing stale effects,
    // these bus writes provide the required >500 us wake time at 400 kHz.
    for (std::size_t slot = 0; slot < sizeof(sequence); ++slot) {
      if (!writeRegister(static_cast<uint8_t>(0x04 + slot), sequence[slot])) return false;
    }
    return writeRegister(0x0c, 0x01);
  }

  bool readRegister(uint8_t reg, uint8_t& value) {
    if (!wire_) return false;
    const uint16_t previous_timeout = wire_->getTimeOut();
    wire_->setTimeOut(10);
    wire_->beginTransmission(0x5a);
    const bool written = wire_->write(reg) == 1;
    const bool sent = wire_->endTransmission(false) == 0;
    bool ok = written && sent &&
        wire_->requestFrom(uint8_t(0x5a), std::size_t(1), true) == 1;
    if (ok) {
      const int result = wire_->read();
      ok = result >= 0;
      if (ok) value = static_cast<uint8_t>(result);
    }
    while (wire_->available()) wire_->read();
    wire_->setTimeOut(previous_timeout);
    return ok;
  }

  bool writeRegister(uint8_t reg, uint8_t value) {
    if (!wire_) return false;
    const uint16_t previous_timeout = wire_->getTimeOut();
    wire_->setTimeOut(10);
    wire_->beginTransmission(0x5a);
    const bool written = wire_->write(reg) == 1 && wire_->write(value) == 1;
    const bool sent = wire_->endTransmission(true) == 0;
    wire_->setTimeOut(previous_timeout);
    return written && sent;
  }

  TwoWire* wire_ = nullptr;
  bool ready_ = false;
  bool alert_active_ = false;
  uint8_t batches_left_ = 0;
};

}  // namespace watch
