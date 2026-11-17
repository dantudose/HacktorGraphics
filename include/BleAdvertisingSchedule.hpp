#pragma once

#include <cstdint>

#include "WatchTiming.hpp"

namespace watch {

// Schedule brief legacy connectable advertisements, not BLE Periodic
// Advertising. The caller completes radio shutdown before beginning a new
// window and never enters processor light sleep while the controller is on.
class BleAdvertisingSchedule {
 public:
  static constexpr uint32_t kIntervalMs = 5000;
  static constexpr uint32_t kBurstMs = 3000;
  static constexpr uint32_t kConnectedSessionMs = 30000;

  void setEnabled(bool enabled) {
    if (enabled_ == enabled) return;
    enabled_ = enabled;
    has_window_ = false;
    window_open_ = false;
    connection_seen_ = false;
    connected_ = false;
  }

  bool shouldStart(uint32_t now_ms) const {
    return enabled_ && !window_open_ &&
        (!has_window_ || !remainingMs(now_ms, window_started_ms_, kIntervalMs));
  }

  // Call before the first transport initialization attempt. Failure still
  // consumes this cycle, preventing failed setup from holding the CPU awake.
  // Repeated calls during a window cannot extend its advertising/session cap.
  void beginWindow(uint32_t now_ms) {
    if (!shouldStart(now_ms)) return;
    has_window_ = true;
    window_open_ = true;
    connection_seen_ = false;
    connected_ = false;
    window_started_ms_ = now_ms;
  }

  bool keepRadio(uint32_t now_ms, bool connected) {
    if (!enabled_ || !window_open_) return false;
    const uint32_t burst_wait = remainingMs(now_ms, window_started_ms_, kBurstMs);
    // A late connection must not reopen a window whose unconnected deadline
    // has passed, even if the main loop was delayed before observing it.
    if (!connection_seen_ && !burst_wait) return closeWindow();
    if (connected && !connection_seen_) {
      connection_seen_ = true;
      connection_started_ms_ = now_ms;
    }
    connected_ = connected;
    if (connection_seen_ &&
        !remainingMs(now_ms, connection_started_ms_, kConnectedSessionMs))
      return closeWindow();
    // After the initial burst, retain the radio only for the attached phone.
    // A reconnect during that burst still shares the first connection's cap.
    if (!connected && !burst_wait) return closeWindow();
    return true;
  }

  uint32_t waitMs(uint32_t now_ms) const {
    if (!enabled_) return UINT32_MAX;
    if (!has_window_) return 0;
    if (window_open_) {
      return connection_seen_ && connected_
          ? remainingMs(now_ms, connection_started_ms_, kConnectedSessionMs)
          : remainingMs(now_ms, window_started_ms_, kBurstMs);
    }
    return remainingMs(now_ms, window_started_ms_, kIntervalMs);
  }

 private:
  bool closeWindow() {
    window_open_ = false;
    connected_ = false;
    return false;
  }

  uint32_t window_started_ms_ = 0;
  uint32_t connection_started_ms_ = 0;
  bool enabled_ = false;
  bool has_window_ = false;
  bool window_open_ = false;
  bool connection_seen_ = false;
  bool connected_ = false;
};

}  // namespace watch
