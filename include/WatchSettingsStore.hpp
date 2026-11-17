#pragma once

#include "WatchSettings.hpp"

namespace watch {

// Store is Preferences-like, but remains independent of Arduino for testing.
// Loading never writes flash, including when sanitizing an older/bad value.
template <typename Store>
WatchSettings loadSettings(Store& prefs) {
  WatchSettings value;
  value.airplane = prefs.getBool("airplane", false);
  value.vibration = prefs.getBool("vibration", true);
  value.motion_wake = prefs.getBool("motionwake", true);
  value.brightness = prefs.getUChar("brightness", 100);
  value.timeout_seconds = prefs.getUChar("timeout", 10);
  return sanitizeSettings(value);
}

// Commit only changed fields. Keep the last successful snapshot intact on a
// partial failure so the caller can report it and retry all necessary fields.
template <typename Store>
bool saveSettings(Store& prefs, const WatchSettings& value,
                  WatchSettings& last_saved) {
  bool ok = true;
  if (value.airplane != last_saved.airplane)
    ok &= prefs.putBool("airplane", value.airplane) == 1;
  if (value.vibration != last_saved.vibration)
    ok &= prefs.putBool("vibration", value.vibration) == 1;
  if (value.motion_wake != last_saved.motion_wake)
    ok &= prefs.putBool("motionwake", value.motion_wake) == 1;
  if (value.brightness != last_saved.brightness)
    ok &= prefs.putUChar("brightness", value.brightness) == 1;
  if (value.timeout_seconds != last_saved.timeout_seconds)
    ok &= prefs.putUChar("timeout", value.timeout_seconds) == 1;
  if (ok) last_saved = value;
  return ok;
}

}  // namespace watch
