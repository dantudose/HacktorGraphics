#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace NIMBLE_PROPERTY {
static constexpr uint32_t READ = 2;
}

class NimBLEServer;
class NimBLEAdvertising;

namespace ble_test {
struct State {
  bool initialized = false;
  bool fail_init = false;
  bool fail_advertise = false;
  bool fail_server_start = false;
  bool fail_host_stop = false;
  bool fail_clear = false;
  bool disconnect_immediately = false;
  unsigned init_calls = 0;
  unsigned host_stop_calls = 0;
  unsigned clear_calls = 0;
  unsigned disconnect_calls = 0;
  unsigned parameter_calls = 0;
  unsigned advertise_calls = 0;
  uint16_t interval_min = 0;
  uint16_t interval_max = 0;
  uint16_t latency = 0;
  uint16_t timeout = 0;
  NimBLEServer* server = nullptr;
  NimBLEAdvertising* advertising = nullptr;
};
inline State& state() { static State value; return value; }
}

class NimBLEConnInfo {};
class NimBLECharacteristic;

class NimBLECharacteristicCallbacks {
 public:
  virtual ~NimBLECharacteristicCallbacks() {}
  virtual void onRead(NimBLECharacteristic*, NimBLEConnInfo&) {}
};

class NimBLEDescriptor {
 public:
  virtual ~NimBLEDescriptor() {}
  void setValue(const uint8_t* data, size_t length) {
    value.assign(data, data + length);
  }
  void setValue(const char* text) {
    setValue(reinterpret_cast<const uint8_t*>(text), std::strlen(text));
  }
  std::vector<uint8_t> value;
};

class NimBLE2904 : public NimBLEDescriptor {};

class NimBLECharacteristic : public NimBLEDescriptor {
 public:
  NimBLECharacteristic(uint32_t properties, uint16_t maximum)
      : properties(properties), maximum(maximum) {}
  void setCallbacks(NimBLECharacteristicCallbacks* value) { callbacks = value; }
  NimBLEDescriptor* createDescriptor(const char* uuid, uint32_t properties) {
    descriptor_properties[uuid] = properties;
    descriptors[uuid].reset(new NimBLEDescriptor);
    return descriptors[uuid].get();
  }
  NimBLE2904* create2904() {
    NimBLE2904* format = new NimBLE2904;
    descriptors["2904"].reset(format);
    return format;
  }
  void simulateRead() {
    NimBLEConnInfo info;
    callbacks->onRead(this, info);
  }
  uint32_t properties;
  uint16_t maximum;
  NimBLECharacteristicCallbacks* callbacks = nullptr;
  std::map<std::string, std::unique_ptr<NimBLEDescriptor>> descriptors;
  std::map<std::string, uint32_t> descriptor_properties;
};

class NimBLEService {
 public:
  NimBLECharacteristic* createCharacteristic(const char* uuid, uint32_t properties,
                                            uint16_t maximum) {
    characteristics[uuid].reset(new NimBLECharacteristic(properties, maximum));
    return characteristics[uuid].get();
  }
  std::map<std::string, std::unique_ptr<NimBLECharacteristic>> characteristics;
};

class NimBLEServer {
 public:
  void advertiseOnDisconnect(bool value) { auto_advertise = value; }
  NimBLEService* createService(const char* uuid) {
    services[uuid].reset(new NimBLEService);
    return services[uuid].get();
  }
  bool start() { return !ble_test::state().fail_server_start; }
  std::vector<uint16_t> getPeerDevices() const { return peers; }
  uint8_t getConnectedCount() const { return static_cast<uint8_t>(peers.size()); }
  bool disconnect(uint16_t peer) {
    ++ble_test::state().disconnect_calls;
    if (ble_test::state().disconnect_immediately)
      peers.erase(std::remove(peers.begin(), peers.end(), peer), peers.end());
    return true;
  }
  void updateConnParams(uint16_t, uint16_t minimum, uint16_t maximum,
                        uint16_t latency, uint16_t timeout) {
    auto& state = ble_test::state();
    ++state.parameter_calls;
    state.interval_min = minimum;
    state.interval_max = maximum;
    state.latency = latency;
    state.timeout = timeout;
  }
  bool auto_advertise = true;
  std::vector<uint16_t> peers;
  std::map<std::string, std::unique_ptr<NimBLEService>> services;
};

class NimBLEAdvertising {
 public:
  void setAdvertisingInterval(uint16_t value) { interval = value; }
  void enableScanResponse(bool value) { scan_response = value; }
  bool addServiceUUID(const char* value) { service_uuid = value; return true; }
  bool setPreferredParams(uint16_t minimum, uint16_t maximum) {
    preferred_minimum = minimum;
    preferred_maximum = maximum;
    return true;
  }
  bool setName(const char* value) { name = value; return true; }
  bool start() {
    ++ble_test::state().advertise_calls;
    if (ble_test::state().fail_advertise) return false;
    advertising = true;
    return true;
  }
  bool stop() { advertising = false; return true; }
  bool isAdvertising() const { return advertising; }
  uint16_t interval = 0;
  uint16_t preferred_minimum = 0;
  uint16_t preferred_maximum = 0;
  bool scan_response = false;
  bool advertising = false;
  std::string service_uuid;
  std::string name;
};

class NimBLEDevice {
 public:
  static bool isInitialized() { return ble_test::state().initialized; }
  static bool init(const char*) {
    auto& state = ble_test::state();
    ++state.init_calls;
    if (state.fail_init) return false;
    state.initialized = true;
    return true;
  }
  static NimBLEServer* createServer() {
    auto& state = ble_test::state();
    if (!state.server) state.server = new NimBLEServer;
    return state.server;
  }
  static NimBLEAdvertising* getAdvertising() {
    auto& state = ble_test::state();
    if (!state.advertising) state.advertising = new NimBLEAdvertising;
    return state.advertising;
  }
  static bool deinit(bool clear) {
    auto& state = ble_test::state();
    if (!clear) {
      ++state.host_stop_calls;
      if (state.fail_host_stop) return false;
      state.initialized = false;
      if (state.advertising) state.advertising->advertising = false;
    } else {
      ++state.clear_calls;
      if (state.fail_clear) return false;
      delete state.server;
      delete state.advertising;
      state.server = nullptr;
      state.advertising = nullptr;
    }
    return true;
  }
};

namespace ble_test {
inline void reset() {
  delete state().server;
  delete state().advertising;
  state() = State();
}
}
