#pragma once

#include <cstddef>
#include <cstdint>

namespace watch {

struct TouchPoint {
  bool pressed = false;
  int16_t x = 0;
  int16_t y = 0;
};

// Decode five consecutive CST816 registers starting at FingerNum (0x02).
// Rotation follows the LCD: 0 is native, 2 turns both axes through 180 degrees.
// Invalid packets leave the previous output untouched; they are not releases.
inline bool decodeCst816Point(const uint8_t* bytes, std::size_t length,
                             TouchPoint& point, uint8_t rotation = 2) {
  if (!bytes || length < 5 || bytes[0] > 1) return false;
  TouchPoint next;
  if (bytes[0] == 0) {
    point = next;
    return true;
  }
  // Some CST816 firmware reports a last point with an explicit Up flag while
  // FingerNum is still one. Up coordinates may be stale/invalid and are unused.
  // CST816T register guide: XposH[7:6] = Down 0, Up 1, Contact 2, reserved 3.
  const uint8_t event = bytes[1] >> 6;
  if (event == 1) {
    point = next;
    return true;
  }
  if (event == 3) return false;
  const int16_t raw_x = static_cast<int16_t>(((bytes[1] & 0x0f) << 8) | bytes[2]);
  const int16_t raw_y = static_cast<int16_t>(((bytes[3] & 0x0f) << 8) | bytes[4]);
  if (raw_x >= 240 || raw_y >= 240) return false;
  next.pressed = true;
  switch (rotation & 3) {
    case 0: next.x = raw_x; next.y = raw_y; break;
    case 1: next.x = raw_y; next.y = 239 - raw_x; break;
    case 2: next.x = 239 - raw_x; next.y = 239 - raw_y; break;
    case 3: next.x = 239 - raw_y; next.y = raw_x; break;
  }
  point = next;
  return true;
}

}  // namespace watch
