#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

// Native-only register bus for testing the actual WatchHaptics driver. It
// records writes and can fail a register transfer or all register reads.
class TwoWire {
 public:
  uint8_t regs[256] = {};
  std::vector<std::pair<uint8_t, uint8_t>> writes;
  int fail_reg = -1;
  bool fail_reads = false;
  uint16_t timeout = 173;

  TwoWire() { regs[0] = 0x60; } // DRV2605 device ID.

  uint16_t getTimeOut() const { return timeout; }
  void setTimeOut(uint16_t value) { timeout = value; }

  void beginTransmission(uint8_t address) {
    assert(address == 0x5a);
    tx_.clear();
  }

  std::size_t write(uint8_t value) {
    tx_.push_back(value);
    return 1;
  }

  uint8_t endTransmission(bool) {
    assert(!tx_.empty());
    selected_ = tx_[0];
    if (selected_ == fail_reg) return 4;
    if (tx_.size() == 2) {
      regs[selected_] = tx_[1];
      writes.emplace_back(selected_, tx_[1]);
    }
    return 0;
  }

  std::size_t requestFrom(uint8_t address, std::size_t length, bool) {
    assert(address == 0x5a && length == 1);
    rx_ = !fail_reads;
    return rx_ ? 1 : 0;
  }

  int read() {
    if (!rx_) return -1;
    rx_ = false;
    return regs[selected_];
  }

  int available() const { return rx_; }

 private:
  std::vector<uint8_t> tx_;
  uint8_t selected_ = 0;
  bool rx_ = false;
};
