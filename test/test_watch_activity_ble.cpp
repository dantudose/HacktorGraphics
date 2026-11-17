#include "WatchActivityBle.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {

NimBLECharacteristic& stepsCharacteristic() {
  return *ble_test::state().server->services.at(watch::activityServiceUuid())
      ->characteristics.at(watch::activityStepsUuid());
}

NimBLECharacteristic& distanceCharacteristic() {
  return *ble_test::state().server->services.at(watch::activityServiceUuid())
      ->characteristics.at(watch::activityDistanceUuid());
}

void finish(watch::WatchActivityBle& ble, uint32_t now) {
  ble_test::state().server->peers.clear();
  ble.requestStop(now);
  ble.service(now);
  assert(!ble.ownsRadio() && !ble.closing() && !NimBLEDevice::isInitialized());
}

void startsAReadableServiceAndSlowAdvertising() {
  ble_test::reset();
  watch::WatchActivityBle ble;
  assert(ble.start(0, 123));
  const auto& state = ble_test::state();
  assert(ble.ownsRadio() && ble.active() && !ble.closing() && !ble.connected());
  assert(!state.server->auto_advertise);
  assert(state.server->services.size() == 1);
  assert(state.server->services.at(watch::activityServiceUuid())->characteristics.size() == 2);
  assert(state.advertising->interval == 1600 && state.advertising->advertising);
  assert(state.advertising->scan_response && state.advertising->name == "HacktorWatch");
  assert(state.advertising->service_uuid == watch::activityServiceUuid());
  const auto& steps = stepsCharacteristic();
  const auto& distance = distanceCharacteristic();
  assert(steps.properties == NIMBLE_PROPERTY::READ && steps.maximum == 4);
  assert(distance.properties == NIMBLE_PROPERTY::READ && distance.maximum == 8);
  assert(steps.descriptor_properties.at("2901") == NIMBLE_PROPERTY::READ);
  assert(std::memcmp(steps.descriptors.at("2904")->value.data(),
                     watch::activityStepsPresentation(), 7) == 0);
  assert(std::memcmp(distance.descriptors.at("2904")->value.data(),
                     watch::activityDistancePresentation(), 7) == 0);
  finish(ble, 10);
}

void mainUpdatesOnlyTheAtomicAndReadCallbacksPublishCompleteValues() {
  ble_test::reset();
  watch::WatchActivityBle ble;
  assert(ble.start(10, 1));
  const auto before = stepsCharacteristic().value;
  ble.updateValues(0x12345678);
  assert(stepsCharacteristic().value == before);
  stepsCharacteristic().simulateRead();
  distanceCharacteristic().simulateRead();
  const auto expected = watch::encodeActivityData(0x12345678);
  assert(stepsCharacteristic().value.size() == sizeof(expected.steps));
  assert(distanceCharacteristic().value.size() == sizeof(expected.distance));
  assert(std::memcmp(stepsCharacteristic().value.data(), expected.steps, 4) == 0);
  assert(std::memcmp(distanceCharacteristic().value.data(), expected.distance, 8) == 0);
  assert(ble_test::state().init_calls == 1);
  finish(ble, 20);
}

void connectsRequestsConservativeParametersAndReadvertisesAfterDisconnect() {
  ble_test::reset();
  watch::WatchActivityBle ble;
  assert(ble.start(0, 0));
  auto& state = ble_test::state();
  state.server->peers.push_back(7);
  state.advertising->advertising = false;
  ble.service(10);
  assert(ble.connected() && state.parameter_calls == 1);
  assert(state.interval_min == 160 && state.interval_max == 320);
  assert(state.latency == 0 && state.timeout == 600);
  ble.service(260);
  assert(state.parameter_calls == 1 && state.advertise_calls == 1);
  state.server->peers.clear();
  ble.service(1010);
  assert(!ble.connected() && state.advertise_calls == 2);
  state.server->peers.push_back(8);
  state.advertising->advertising = false;
  ble.service(1260);
  assert(state.parameter_calls == 2);
  finish(ble, 1300);
}

void screenOffWaitsForGracefulDisconnectBeforeDeletingObjects() {
  ble_test::reset();
  watch::WatchActivityBle ble;
  assert(ble.start(0, 10));
  auto& state = ble_test::state();
  state.server->peers.push_back(3);
  ble.requestStop(100);
  assert(ble.closing() && !ble.active() && !state.advertising->advertising);
  ble.service(100);
  assert(state.disconnect_calls == 1 && state.host_stop_calls == 0);
  ble.service(101);
  assert(state.disconnect_calls == 1 && state.host_stop_calls == 0);
  assert(ble.ownsRadio() && state.server);
  state.server->peers.clear();
  ble.service(200);
  assert(!ble.ownsRadio() && !ble.closing() && state.server == nullptr);
  assert(state.host_stop_calls == 1 && state.clear_calls == 1);
}

void aStuckConnectionGetsABoundedShutdownFallback() {
  ble_test::reset();
  watch::WatchActivityBle ble;
  assert(ble.start(0, 1));
  auto& state = ble_test::state();
  state.server->peers.push_back(1);
  ble.requestStop(100);
  ble.service(100);
  ble.service(349);
  assert(state.disconnect_calls == 1 && state.host_stop_calls == 0);
  ble.service(350);
  assert(state.disconnect_calls == 2 && state.host_stop_calls == 0);
  ble.service(2099);
  assert(ble.ownsRadio() && state.host_stop_calls == 0);
  ble.service(2100);
  assert(!ble.ownsRadio() && state.host_stop_calls == 1);
}

void failedHostShutdownKeepsTheObjectsAndRetriesSlowly() {
  ble_test::reset();
  watch::WatchActivityBle ble;
  assert(ble.start(0, 1));
  auto& state = ble_test::state();
  state.fail_host_stop = true;
  NimBLEServer* original = state.server;
  ble.requestStop(100);
  ble.service(100);
  assert(ble.ownsRadio() && ble.closing() && state.server == original);
  assert(state.host_stop_calls == 1 && state.clear_calls == 0);
  stepsCharacteristic().simulateRead(); // Callback owner is still alive.
  ble.service(1099);
  assert(state.host_stop_calls == 1);
  state.fail_host_stop = false;
  ble.service(1100);
  assert(!ble.ownsRadio() && !state.server && state.clear_calls == 1);
}

void failedObjectClearAlsoRetainsTheLeaseUntilSuccessfulCleanup() {
  ble_test::reset();
  watch::WatchActivityBle ble;
  assert(ble.start(0, 1));
  auto& state = ble_test::state();
  state.fail_clear = true;
  ble.requestStop(10);
  ble.service(10);
  assert(ble.ownsRadio() && ble.closing() && !state.initialized && state.server);
  state.fail_clear = false;
  ble.service(1009);
  assert(state.clear_calls == 1);
  ble.service(1010);
  assert(!ble.ownsRadio() && !state.server && state.clear_calls == 2);
}

void failedStartsCleanUpAndCannotSpinOrTakeAnotherOwnersHost() {
  ble_test::reset();
  watch::WatchActivityBle ble;
  auto& state = ble_test::state();
  state.fail_advertise = true;
  assert(!ble.start(100, 3));
  assert(!ble.ownsRadio() && !state.initialized && !state.server);
  assert(state.init_calls == 1 && state.clear_calls == 1);
  state.fail_advertise = false;
  assert(!ble.start(1099, 4) && state.init_calls == 1);
  assert(ble.start(1100, 4));
  finish(ble, 1200);
  state.initialized = true; // A CTS operation owns this host.
  assert(!ble.start(2200, 5));
  assert(!ble.ownsRadio() && state.init_calls == 2 && state.initialized);
  state.initialized = false;
}

void lifecycleDeadlinesSurviveTheMillisRollover() {
  ble_test::reset();
  watch::WatchActivityBle ble;
  const uint32_t begin = std::numeric_limits<uint32_t>::max() - 100;
  assert(ble.start(begin, 10));
  auto& state = ble_test::state();
  state.server->peers.push_back(7);
  state.fail_host_stop = true;
  ble.requestStop(begin + 1);
  ble.service(begin + 1);
  ble.service(begin + 2001);
  assert(state.host_stop_calls == 1 && ble.ownsRadio());
  state.fail_host_stop = false;
  ble.service(begin + 3000);
  assert(state.host_stop_calls == 1);
  ble.service(begin + 3001);
  assert(state.host_stop_calls == 2 && !ble.ownsRadio());
}

}  // namespace

int main() {
  startsAReadableServiceAndSlowAdvertising();
  mainUpdatesOnlyTheAtomicAndReadCallbacksPublishCompleteValues();
  connectsRequestsConservativeParametersAndReadvertisesAfterDisconnect();
  screenOffWaitsForGracefulDisconnectBeforeDeletingObjects();
  aStuckConnectionGetsABoundedShutdownFallback();
  failedHostShutdownKeepsTheObjectsAndRetriesSlowly();
  failedObjectClearAlsoRetainsTheLeaseUntilSuccessfulCleanup();
  failedStartsCleanUpAndCannotSpinOrTakeAnotherOwnersHost();
  lifecycleDeadlinesSurviveTheMillisRollover();
  ble_test::reset();
  std::puts("Watch activity BLE: all 9 scenarios passed.");
}
