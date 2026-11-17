#include "WatchTouchData.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <initializer_list>

namespace {

void singleTouchUsesDisplayRotation() {
  const uint8_t data[] = {1, 0, 25, 0, 80};
  watch::TouchPoint point;
  assert(watch::decodeCst816Point(data, sizeof(data), point));
  assert(point.pressed && point.x == 214 && point.y == 159);
}

void releaseIgnoresStaleCoordinates() {
  const uint8_t data[] = {0, 0xff, 0xff, 0xff, 0xff};
  watch::TouchPoint point;
  point.pressed = true;
  assert(watch::decodeCst816Point(data, sizeof(data), point));
  assert(!point.pressed && point.x == 0 && point.y == 0);
}

void eventBitsAreNotCoordinateBits() {
  const uint8_t data[] = {1, 0x80, 25, 0xb0, 80};
  watch::TouchPoint point;
  assert(watch::decodeCst816Point(data, sizeof(data), point));
  assert(point.pressed && point.x == 214 && point.y == 159);
}

void touchUpCanReleaseTheLastReportedFinger() {
  const uint8_t data[] = {1, 0x4f, 0xff, 0xff, 0xff};
  watch::TouchPoint point;
  point.pressed = true;
  point.x = 123;
  point.y = 45;
  assert(watch::decodeCst816Point(data, sizeof(data), point));
  assert(!point.pressed && point.x == 0 && point.y == 0);
}

void invalidDataDoesNotCreateARelease() {
  const uint8_t packets[][5] = {
      {0xff, 0xff, 0xff, 0xff, 0xff}, // CST816T idle/invalid firmware response.
      {2, 0, 25, 0, 80},             // Single-touch device cannot report two.
      {1, 0, 240, 0, 80},            // Outside the panel, do not clamp to a key.
      {1, 0, 25, 0, 240},
      {1, 1, 25, 0, 80},             // High coordinate nibble must be decoded.
      {1, 0, 25, 1, 80},
      {1, 0xc0, 25, 0, 80},          // Reserved event is not a valid release.
  };
  for (const auto& packet : packets) {
    watch::TouchPoint point;
    point.pressed = true;
    point.x = 123;
    point.y = 45;
    assert(!watch::decodeCst816Point(packet, sizeof(packet), point));
    assert(point.pressed && point.x == 123 && point.y == 45);
  }
}

void shortOrMissingPacketsAreRejected() {
  const uint8_t data[] = {1, 0, 25, 0, 80};
  watch::TouchPoint point;
  point.pressed = true;
  assert(!watch::decodeCst816Point(nullptr, 5, point));
  for (unsigned length = 0; length < 5; ++length) {
    assert(!watch::decodeCst816Point(data, length, point));
    assert(point.pressed);
  }
}

void anIdleFrameBetweenContactAndUpCannotInventARelease() {
  const uint8_t down[] = {1, 0, 25, 0, 80};
  const uint8_t idle[] = {0xff, 0xff, 0xff, 0xff, 0xff};
  const uint8_t contact[] = {1, 0x80, 26, 0, 81};
  const uint8_t up[] = {1, 0x40, 26, 0, 81};
  watch::TouchPoint point;
  assert(watch::decodeCst816Point(down, sizeof(down), point));
  assert(point.pressed);
  assert(!watch::decodeCst816Point(idle, sizeof(idle), point));
  assert(point.pressed && point.x == 214 && point.y == 159);
  assert(watch::decodeCst816Point(contact, sizeof(contact), point));
  assert(point.pressed && point.x == 213 && point.y == 158);
  assert(watch::decodeCst816Point(up, sizeof(up), point));
  assert(!point.pressed);
}

void allRotationsAndPanelCornersAreBounded() {
  const int16_t expected_x[] = {25, 80, 214, 159};
  const int16_t expected_y[] = {80, 214, 159, 25};
  const uint8_t data[] = {1, 0, 25, 0, 80};
  watch::TouchPoint point;
  for (uint8_t rotation = 0; rotation < 4; ++rotation) {
    assert(watch::decodeCst816Point(data, sizeof(data), point, rotation));
    assert(point.x == expected_x[rotation] && point.y == expected_y[rotation]);
    for (uint8_t x : {uint8_t(0), uint8_t(239)}) {
      for (uint8_t y : {uint8_t(0), uint8_t(239)}) {
        const uint8_t corner[] = {1, 0, x, 0, y};
        assert(watch::decodeCst816Point(corner, sizeof(corner), point, rotation));
        assert((point.x == 0 || point.x == 239) && (point.y == 0 || point.y == 239));
      }
    }
  }
}

}  // namespace

int main() {
  singleTouchUsesDisplayRotation();
  releaseIgnoresStaleCoordinates();
  eventBitsAreNotCoordinateBits();
  touchUpCanReleaseTheLastReportedFinger();
  invalidDataDoesNotCreateARelease();
  shortOrMissingPacketsAreRejected();
  anIdleFrameBetweenContactAndUpCannotInventARelease();
  allRotationsAndPanelCornersAreBounded();
  std::puts("Watch touch: all 8 scenarios passed.");
}
