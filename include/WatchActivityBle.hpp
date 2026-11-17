#pragma once

#include <NimBLEDevice.h>

#include <atomic>
#include <cstdint>

#include "ActivityData.hpp"

namespace watch {

// Own the radio only during a visible inspection session. CTS and airplane
// handling must wait for ownsRadio() to become false before using/deleting BLE.
// Main-loop methods never run in a NimBLE callback.
class WatchActivityBle {
 public:
  WatchActivityBle() : steps_read_(latest_steps_, false),
                       distance_read_(latest_steps_, true) {}

  bool start(uint32_t now_ms, uint32_t steps) {
    updateValues(steps);
    if (owns_radio_) return active();
    if (NimBLEDevice::isInitialized() ||
        (start_attempted_ && now_ms - last_start_attempt_ms_ < kRetryMs)) return false;
    start_attempted_ = true;
    last_start_attempt_ms_ = now_ms;
    owns_radio_ = true;

    if (!NimBLEDevice::init("HacktorWatch")) return failedStart(now_ms);
    server_ = NimBLEDevice::createServer();
    if (!server_) return failedStart(now_ms);
    server_->advertiseOnDisconnect(false);
    NimBLEService* service = server_->createService(activityServiceUuid());
    if (!service) return failedStart(now_ms);
    steps_characteristic_ = service->createCharacteristic(
        activityStepsUuid(), NIMBLE_PROPERTY::READ, 4);
    distance_characteristic_ = service->createCharacteristic(
        activityDistanceUuid(), NIMBLE_PROPERTY::READ, 8);
    if (!steps_characteristic_ || !distance_characteristic_)
      return failedStart(now_ms);

    const ActivityData data = encodeActivityData(steps);
    steps_characteristic_->setValue(data.steps, sizeof(data.steps));
    distance_characteristic_->setValue(data.distance, sizeof(data.distance));
    steps_characteristic_->setCallbacks(&steps_read_);
    distance_characteristic_->setCallbacks(&distance_read_);
    if (!describe(steps_characteristic_, "Daily steps", activityStepsPresentation()) ||
        !describe(distance_characteristic_, "Estimated distance", activityDistancePresentation()))
      return failedStart(now_ms);
    if (!server_->start()) return failedStart(now_ms);

    advertising_ = NimBLEDevice::getAdvertising();
    if (!advertising_) return failedStart(now_ms);
    advertising_->setAdvertisingInterval(1600); // 1 s in 0.625 ms units.
    // The full name is in scan response because the 128-bit service UUID and
    // name together exceed the 31-byte legacy advertising payload.
    advertising_->enableScanResponse(true);
    if (!advertising_->addServiceUUID(activityServiceUuid()) ||
        !advertising_->setPreferredParams(160, 320) ||
        !advertising_->setName("HacktorWatch")) return failedStart(now_ms);
    if (!advertising_->start()) return failedStart(now_ms);
    last_advertising_attempt_ms_ = now_ms;
    advertising_attempted_ = true;
    return true;
  }

  // Publish only one atomic scalar. Encoding and characteristic mutation run
  // in onRead(), on the BLE host thread, never alongside a host GATT read.
  void updateValues(uint32_t steps) {
    latest_steps_.store(steps, std::memory_order_relaxed);
  }

  void requestStop(uint32_t now_ms) {
    if (!owns_radio_ || closing_) return;
    closing_ = true;
    stop_started_ms_ = now_ms;
    disconnect_attempted_ = false;
    shutdown_attempted_ = false;
    if (NimBLEDevice::isInitialized() && advertising_) advertising_->stop();
  }

  void service(uint32_t now_ms) {
    if (!owns_radio_) return;
    if (closing_) {
      serviceStop(now_ms);
      return;
    }
    if (!NimBLEDevice::isInitialized()) {
      requestStop(now_ms);
      serviceStop(now_ms);
      return;
    }

    if (peer_poll_attempted_ && now_ms - last_peer_poll_ms_ < kPeerPollMs) return;
    peer_poll_attempted_ = true;
    last_peer_poll_ms_ = now_ms;
    const auto peers = server_->getPeerDevices();
    if (!peers.empty()) {
      // Advertising stops at connection and resumes only after disconnection,
      // so the inspection service admits a single phone at a time.
      if (configured_peer_ != peers.front()) {
        configured_peer_ = peers.front();
        server_->updateConnParams(configured_peer_, 160, 320, 0, 600);
      }
      return;
    }
    configured_peer_ = kNoPeer;
    if (!advertising_->isAdvertising() &&
        (!advertising_attempted_ || now_ms - last_advertising_attempt_ms_ >= kRetryMs)) {
      advertising_attempted_ = true;
      last_advertising_attempt_ms_ = now_ms;
      advertising_->start();
    }
  }

  bool ownsRadio() const { return owns_radio_; }
  bool active() const { return owns_radio_ && !closing_; }
  bool closing() const { return closing_; }
  bool connected() const {
    return owns_radio_ && server_ && NimBLEDevice::isInitialized() &&
        server_->getConnectedCount() != 0;
  }

 private:
  class ReadCachedValue : public NimBLECharacteristicCallbacks {
   public:
    ReadCachedValue(const std::atomic<uint32_t>& steps, bool distance)
        : steps_(steps), distance_(distance) {}

    void onRead(NimBLECharacteristic* characteristic, NimBLEConnInfo&) override {
      const ActivityData data = encodeActivityData(steps_.load(std::memory_order_relaxed));
      if (distance_) characteristic->setValue(data.distance, sizeof(data.distance));
      else characteristic->setValue(data.steps, sizeof(data.steps));
    }

   private:
    const std::atomic<uint32_t>& steps_;
    bool distance_;
  };

  static bool describe(NimBLECharacteristic* characteristic, const char* label,
                       const uint8_t* presentation) {
    NimBLEDescriptor* description = characteristic->createDescriptor("2901", NIMBLE_PROPERTY::READ);
    NimBLE2904* format = characteristic->create2904();
    if (!description || !format) return false;
    description->setValue(label);
    format->setValue(presentation, kActivityPresentationSize);
    return true;
  }

  bool failedStart(uint32_t now_ms) {
    requestStop(now_ms);
    serviceStop(now_ms);
    return false;
  }

  void serviceStop(uint32_t now_ms) {
    if (NimBLEDevice::isInitialized()) {
      if (server_ && server_->getConnectedCount()) {
        if (!disconnect_attempted_ || now_ms - last_disconnect_ms_ >= kPeerPollMs) {
          disconnect_attempted_ = true;
          last_disconnect_ms_ = now_ms;
          if (advertising_) advertising_->stop();
          for (uint16_t peer : server_->getPeerDevices()) server_->disconnect(peer);
        }
        // Normally the host reports disconnection while the main loop keeps
        // servicing timers/haptics. A stuck link gets bounded host shutdown;
        // that exceptional path can block for NimBLE's own stop timeout.
        if (server_->getConnectedCount() && now_ms - stop_started_ms_ < kDisconnectGraceMs) return;
      }
    }

    if (shutdown_attempted_ && now_ms - last_shutdown_ms_ < kRetryMs) return;
    shutdown_attempted_ = true;
    last_shutdown_ms_ = now_ms;
    // Stop the host before deleting attributes referenced by callbacks. Keep
    // every object and the radio lease if shutdown fails, then retry slowly.
    if (!NimBLEDevice::deinit(false) || !NimBLEDevice::deinit(true)) return;
    server_ = nullptr;
    advertising_ = nullptr;
    steps_characteristic_ = nullptr;
    distance_characteristic_ = nullptr;
    owns_radio_ = false;
    closing_ = false;
    peer_poll_attempted_ = false;
    advertising_attempted_ = false;
    configured_peer_ = kNoPeer;
  }

  static constexpr uint32_t kRetryMs = 1000;
  static constexpr uint32_t kPeerPollMs = 250;
  static constexpr uint32_t kDisconnectGraceMs = 2000;
  static constexpr uint16_t kNoPeer = 0xffff;

  std::atomic<uint32_t> latest_steps_{0};
  ReadCachedValue steps_read_;
  ReadCachedValue distance_read_;
  NimBLEServer* server_ = nullptr;
  NimBLEAdvertising* advertising_ = nullptr;
  NimBLECharacteristic* steps_characteristic_ = nullptr;
  NimBLECharacteristic* distance_characteristic_ = nullptr;
  uint32_t last_start_attempt_ms_ = 0;
  uint32_t last_advertising_attempt_ms_ = 0;
  uint32_t last_peer_poll_ms_ = 0;
  uint32_t stop_started_ms_ = 0;
  uint32_t last_disconnect_ms_ = 0;
  uint32_t last_shutdown_ms_ = 0;
  uint16_t configured_peer_ = kNoPeer;
  bool owns_radio_ = false;
  bool closing_ = false;
  bool start_attempted_ = false;
  bool advertising_attempted_ = false;
  bool peer_poll_attempted_ = false;
  bool disconnect_attempted_ = false;
  bool shutdown_attempted_ = false;
};

}  // namespace watch
