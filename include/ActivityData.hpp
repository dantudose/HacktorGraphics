#pragma once

#include <cstddef>
#include <cstdint>

namespace watch {

// Shared with the watchface's existing 1.80 m x 0.415 stride estimate.
static constexpr uint32_t kEstimatedStepMillimetres = 747;
static constexpr size_t kActivityPresentationSize = 7;

inline uint64_t activityDistanceMillimetres(uint32_t steps) {
  return static_cast<uint64_t>(steps) * kEstimatedStepMillimetres;
}

struct ActivityData {
  uint8_t steps[4];
  uint8_t distance[8];
};

// Values are independent readable scalars in the custom Watch Activity service,
// not payloads for the SIG's session-based Physical Activity Monitor service.
inline ActivityData encodeActivityData(uint32_t steps) {
  ActivityData value{};
  const uint64_t distance = activityDistanceMillimetres(steps);
  for (size_t i = 0; i < sizeof(value.steps); ++i)
    value.steps[i] = static_cast<uint8_t>(steps >> (i * 8));
  for (size_t i = 0; i < sizeof(value.distance); ++i)
    value.distance[i] = static_cast<uint8_t>(distance >> (i * 8));
  return value;
}

inline const char* activityServiceUuid() {
  return "b44e0001-2cf5-4b4a-9f4d-8a6f4e7d2c10";
}

inline const char* activityStepsUuid() {
  return "b44e0002-2cf5-4b4a-9f4d-8a6f4e7d2c10";
}

inline const char* activityDistanceUuid() {
  return "b44e0003-2cf5-4b4a-9f4d-8a6f4e7d2c10";
}

// Standard Characteristic Presentation Format (0x2904): format, signed decimal
// exponent, unit (u16 LE), namespace, description (u16 LE). Description zero
// means unknown; the separate 0x2901 descriptor supplies each readable label.
inline const uint8_t* activityStepsPresentation() {
  // uint32, exponent 0, unitless, Bluetooth SIG namespace.
  static const uint8_t value[kActivityPresentationSize] = {
      0x08, 0x00, 0x00, 0x27, 0x01, 0x00, 0x00};
  return value;
}

inline const uint8_t* activityDistancePresentation() {
  // uint64 millimetres represented as metres with exponent -3.
  static const uint8_t value[kActivityPresentationSize] = {
      0x0a, 0xfd, 0x01, 0x27, 0x01, 0x00, 0x00};
  return value;
}

}  // namespace watch
