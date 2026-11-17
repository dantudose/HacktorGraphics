#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

class TwoWire {
 public:
  struct Transfer {
    uint8_t reg;
    std::vector<int> bytes;
    bool write_ok = true;
    uint8_t status = 0;
    int response_length = -1;

    Transfer(uint8_t register_address, std::vector<int> response)
        : reg(register_address), bytes(response) {}
  };

  uint16_t timeout = 173;
  std::vector<uint16_t> timeouts;
  std::vector<Transfer> transfers;
  std::vector<uint8_t> addresses;
  unsigned used = 0;
  unsigned request_count = 0;
  bool ended_with_stop = false;

  uint16_t getTimeOut() const { return timeout; }
  void setTimeOut(uint16_t value) {
    timeout = value;
    timeouts.push_back(value);
  }

  void beginTransmission(uint8_t address) {
    assert(timeout == 10 && used < transfers.size());
    addresses.push_back(address);
    current_ = &transfers[used++];
    rx_position_ = 0;
    rx_length_ = 0;
  }

  std::size_t write(uint8_t value) {
    assert(current_ && value == current_->reg);
    return current_->write_ok ? 1 : 0;
  }

  uint8_t endTransmission(bool stop) {
    assert(current_);
    ended_with_stop = stop;
    return current_->status;
  }

  std::size_t requestFrom(uint8_t address, std::size_t length, bool stop) {
    assert(current_ && address == addresses.back() && stop);
    assert((current_->reg == 0x4c && length == 1) ||
           (current_->reg == 0x4b && length == 2));
    ++request_count;
    rx_length_ = current_->bytes.size();
    return current_->response_length >= 0
        ? static_cast<std::size_t>(current_->response_length) : rx_length_;
  }

  int read() {
    if (!current_ || rx_position_ >= rx_length_) return -1;
    return current_->bytes[rx_position_++];
  }

  int available() const { return static_cast<int>(rx_length_ - rx_position_); }

 private:
  Transfer* current_ = nullptr;
  std::size_t rx_position_ = 0;
  std::size_t rx_length_ = 0;
};
