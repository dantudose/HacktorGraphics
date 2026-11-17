#include "CtsBleCleanup.hpp"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

namespace {

struct TestClient {
  bool released = false;
};

struct TestDevice {
  struct State {
    bool host_initialized = true;
    bool server_alive = true;
    bool fail_client_release = false;
    bool fail_host_stop = false;
    bool fail_object_clear = false;
    std::vector<std::string> calls;
  };

  static State& state() {
    static State value;
    return value;
  }

  static bool deleteClient(TestClient* client) {
    assert(client);
    state().calls.push_back("client");
    if (state().fail_client_release) return false;
    client->released = true;
    return true;
  }

  static bool deinit(bool clear_objects) {
    if (!clear_objects) {
      state().calls.push_back("stop");
      if (state().fail_host_stop) return false;
      state().host_initialized = false;
    } else {
      state().calls.push_back("clear");
      assert(!state().host_initialized);
      if (state().fail_object_clear) return false;
      state().server_alive = false;
    }
    return true;
  }

  static void reset() { state() = State(); }
};

void sharedScanFailureLeavesThePeripheralUntouched() {
  TestDevice::reset();
  assert(watch::releaseCtsBle<TestDevice>(static_cast<TestClient*>(nullptr), true));
  assert(TestDevice::state().calls.empty());
  assert(TestDevice::state().host_initialized && TestDevice::state().server_alive);
}

void sharedClientReleasePreservesTheHostAndGattObjects() {
  TestDevice::reset();
  TestClient client;
  assert(watch::releaseCtsBle<TestDevice>(&client, true));
  assert(client.released);
  assert(TestDevice::state().calls == std::vector<std::string>{"client"});
  assert(TestDevice::state().host_initialized && TestDevice::state().server_alive);
}

void failedSharedClientReleaseStillCannotTearDownThePeripheral() {
  TestDevice::reset();
  TestDevice::state().fail_client_release = true;
  TestClient client;
  assert(!watch::releaseCtsBle<TestDevice>(&client, true));
  assert(!client.released);
  assert(TestDevice::state().calls == std::vector<std::string>{"client"});
  assert(TestDevice::state().host_initialized && TestDevice::state().server_alive);
}

void exclusiveScanFailureStopsTheHostWithoutAPrivateClient() {
  TestDevice::reset();
  assert(watch::releaseCtsBle<TestDevice>(static_cast<TestClient*>(nullptr), false));
  const std::vector<std::string> expected{"stop", "clear"};
  assert(TestDevice::state().calls == expected);
  assert(!TestDevice::state().host_initialized && !TestDevice::state().server_alive);
}

void exclusiveClientIsReleasedBeforeHostAndGlobalObjectCleanup() {
  TestDevice::reset();
  TestClient client;
  assert(watch::releaseCtsBle<TestDevice>(&client, false));
  const std::vector<std::string> expected{"client", "stop", "clear"};
  assert(client.released && TestDevice::state().calls == expected);
  assert(!TestDevice::state().host_initialized && !TestDevice::state().server_alive);
}

void failedClientReleaseDoesNotSkipExclusiveHostTeardown() {
  TestDevice::reset();
  TestDevice::state().fail_client_release = true;
  TestClient client;
  assert(!watch::releaseCtsBle<TestDevice>(&client, false));
  const std::vector<std::string> expected{"client", "stop", "clear"};
  assert(!client.released && TestDevice::state().calls == expected);
  assert(!TestDevice::state().host_initialized && !TestDevice::state().server_alive);
}

void failedHostStopPreservesObjectsThatItsCallbacksMayStillReference() {
  TestDevice::reset();
  TestDevice::state().fail_host_stop = true;
  TestClient client;
  assert(!watch::releaseCtsBle<TestDevice>(&client, false));
  const std::vector<std::string> expected{"client", "stop"};
  assert(client.released && TestDevice::state().calls == expected);
  assert(TestDevice::state().host_initialized && TestDevice::state().server_alive);
}

void failedObjectCleanupIsReportedAfterTheHostHasStopped() {
  TestDevice::reset();
  TestDevice::state().fail_object_clear = true;
  TestClient client;
  assert(!watch::releaseCtsBle<TestDevice>(&client, false));
  const std::vector<std::string> expected{"client", "stop", "clear"};
  assert(client.released && TestDevice::state().calls == expected);
  assert(!TestDevice::state().host_initialized && TestDevice::state().server_alive);
}

}  // namespace

int main() {
  sharedScanFailureLeavesThePeripheralUntouched();
  sharedClientReleasePreservesTheHostAndGattObjects();
  failedSharedClientReleaseStillCannotTearDownThePeripheral();
  exclusiveScanFailureStopsTheHostWithoutAPrivateClient();
  exclusiveClientIsReleasedBeforeHostAndGlobalObjectCleanup();
  failedClientReleaseDoesNotSkipExclusiveHostTeardown();
  failedHostStopPreservesObjectsThatItsCallbacksMayStillReference();
  failedObjectCleanupIsReportedAfterTheHostHasStopped();
  std::puts("CTS BLE cleanup: all 8 scenarios passed.");
}
