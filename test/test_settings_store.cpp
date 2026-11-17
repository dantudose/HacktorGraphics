#include "WatchSettingsStore.hpp"

#include <cassert>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

struct TestStore {
  std::map<std::string, uint8_t> values;
  std::set<std::string> failed_keys;
  std::vector<std::string> writes;

  bool getBool(const char* key, bool fallback) const {
    const auto it = values.find(key);
    return it == values.end() ? fallback : it->second != 0;
  }

  uint8_t getUChar(const char* key, uint8_t fallback) const {
    const auto it = values.find(key);
    return it == values.end() ? fallback : it->second;
  }

  unsigned putBool(const char* key, bool value) {
    return putUChar(key, value ? 1 : 0);
  }

  unsigned putUChar(const char* key, uint8_t value) {
    writes.push_back(key);
    if (failed_keys.count(key)) return 0;
    values[key] = value;
    return 1;
  }
};

watch::WatchSettings changedValues() {
  watch::WatchSettings value;
  value.airplane = true;
  value.ble_advertising = false;
  value.vibration = false;
  value.motion_wake = false;
  value.brightness = 34;
  value.timeout_seconds = 30;
  return value;
}

void missingKeysLoadDefaultsWithoutWriting() {
  TestStore store;
  const auto loaded = watch::loadSettings(store);
  const watch::WatchSettings defaults;
  assert(loaded == defaults);
  assert(loaded.ble_advertising);
  assert(store.values.empty() && store.writes.empty());
}

void allSettingsSurviveSavingAndReloading() {
  TestStore store;
  watch::WatchSettings last_saved;
  const auto value = changedValues();
  assert(watch::saveSettings(store, value, last_saved));
  assert(last_saved == value);
  assert(store.writes.size() == 6);
  assert(store.values.at("airplane") == 1);
  assert(store.values.at("bleadvert") == 0);
  assert(store.values.at("vibration") == 0);
  assert(store.values.at("motionwake") == 0);
  assert(store.values.at("brightness") == 34);
  assert(store.values.at("timeout") == 30);
  assert(watch::loadSettings(store) == value);
  assert(store.writes.size() == 6); // Reload has no writes.
}

void invalidStoredBytesAreSanitizedWithoutWriting() {
  TestStore store;
  store.values["brightness"] = 255;
  store.values["timeout"] = 7;
  store.values["airplane"] = 1;
  const auto value = watch::loadSettings(store);
  assert(value.brightness == 100 && value.timeout_seconds == 10);
  assert(value.airplane && value.vibration && value.motion_wake);
  assert(value.ble_advertising);
  assert(store.values.at("brightness") == 255);
  assert(store.values.at("timeout") == 7);
  assert(store.writes.empty());
}

void onlyChangedKeysAreWritten() {
  TestStore store;
  watch::WatchSettings saved;
  auto value = saved;
  value.vibration = false;
  assert(watch::saveSettings(store, value, saved));
  assert(store.writes.size() == 1 && store.writes[0] == "vibration");
  assert(saved == value);
  store.writes.clear();
  value.timeout_seconds = 15;
  assert(watch::saveSettings(store, value, saved));
  assert(store.writes.size() == 1 && store.writes[0] == "timeout");
  assert(saved == value);
}

void unchangedSettingsRequireNoWrites() {
  TestStore store;
  watch::WatchSettings saved = changedValues();
  assert(watch::saveSettings(store, saved, saved));
  assert(store.writes.empty());
  const auto value = saved;
  assert(watch::saveSettings(store, value, saved));
  assert(store.writes.empty());
}

void olderPreferencesEnableAdvertisingWithoutWritingAMigration() {
  TestStore store;
  store.values["airplane"] = 1;
  store.values["vibration"] = 0;
  store.values["motionwake"] = 1;
  store.values["brightness"] = 75;
  store.values["timeout"] = 15;
  const auto value = watch::loadSettings(store);
  assert(value.airplane && value.ble_advertising);
  assert(!value.vibration && value.motion_wake && value.brightness == 75);
  assert(value.timeout_seconds == 15);
  assert(store.values.count("bleadvert") == 0 && store.writes.empty());
}

void advertisingPreferenceSavesAloneAndSurvivesAirplaneChanges() {
  TestStore store;
  watch::WatchSettings saved;
  auto value = saved;
  value.ble_advertising = false;
  assert(value != saved);
  assert(watch::saveSettings(store, value, saved));
  assert(store.writes.size() == 1 && store.writes[0] == "bleadvert");
  assert(!watch::loadSettings(store).ble_advertising);
  store.writes.clear();
  value.airplane = true;
  assert(watch::saveSettings(store, value, saved));
  assert(store.writes.size() == 1 && store.writes[0] == "airplane");
  assert(watch::loadSettings(store).airplane);
  assert(!watch::loadSettings(store).ble_advertising);
  store.writes.clear();
  value.airplane = false;
  assert(watch::saveSettings(store, value, saved));
  assert(store.writes.size() == 1 && store.writes[0] == "airplane");
  assert(!watch::loadSettings(store).ble_advertising);
}

void aFailedAdvertisingWriteKeepsTheSnapshotUntilRetry() {
  TestStore store;
  store.failed_keys.insert("bleadvert");
  watch::WatchSettings saved;
  auto value = saved;
  value.ble_advertising = false;
  assert(!watch::saveSettings(store, value, saved));
  assert(saved.ble_advertising && store.writes.size() == 1);
  store.failed_keys.clear();
  assert(watch::saveSettings(store, value, saved));
  assert(!saved.ble_advertising && !watch::loadSettings(store).ble_advertising);
  assert(store.writes.size() == 2);
  assert(watch::saveSettings(store, value, saved));
  assert(store.writes.size() == 2);
}

void partialFailureKeepsTheSuccessfulSnapshotAndTriesEveryKey() {
  TestStore store;
  store.failed_keys.insert("vibration");
  watch::WatchSettings saved;
  const auto before = saved;
  const auto value = changedValues();
  assert(!watch::saveSettings(store, value, saved));
  assert(saved == before);
  assert(store.writes.size() == 6);
  assert(store.values.at("airplane") == 1);
  assert(store.values.at("bleadvert") == 0);
  assert(store.values.count("vibration") == 0);
  assert(store.values.at("motionwake") == 0);
  assert(store.values.at("brightness") == 34);
  assert(store.values.at("timeout") == 30);
}

void aFailedCommitCanBeRetriedAndThenStopsWriting() {
  TestStore store;
  store.failed_keys.insert("airplane");
  watch::WatchSettings saved;
  const auto value = changedValues();
  assert(!watch::saveSettings(store, value, saved));
  store.failed_keys.clear();
  store.writes.clear();
  assert(watch::saveSettings(store, value, saved));
  assert(store.writes.size() == 6);
  assert(saved == value && watch::loadSettings(store) == value);
  store.writes.clear();
  assert(watch::saveSettings(store, value, saved));
  assert(store.writes.empty());
}

}  // namespace

int main() {
  missingKeysLoadDefaultsWithoutWriting();
  allSettingsSurviveSavingAndReloading();
  invalidStoredBytesAreSanitizedWithoutWriting();
  onlyChangedKeysAreWritten();
  unchangedSettingsRequireNoWrites();
  olderPreferencesEnableAdvertisingWithoutWritingAMigration();
  advertisingPreferenceSavesAloneAndSurvivesAirplaneChanges();
  aFailedAdvertisingWriteKeepsTheSnapshotUntilRetry();
  partialFailureKeepsTheSuccessfulSnapshotAndTriesEveryKey();
  aFailedCommitCanBeRetriedAndThenStopsWriting();
  std::puts("Settings store: all 10 scenarios passed.");
}
