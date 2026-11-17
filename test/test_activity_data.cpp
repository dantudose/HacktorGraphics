#include "ActivityData.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {

uint64_t readLittleEndian(const uint8_t* bytes, size_t size) {
  uint64_t value = 0;
  for (size_t i = 0; i < size; ++i)
    value |= static_cast<uint64_t>(bytes[i]) << (i * 8);
  return value;
}

void zeroStepsProduceZeroValues() {
  const auto data = watch::encodeActivityData(0);
  for (uint8_t value : data.steps) assert(value == 0);
  for (uint8_t value : data.distance) assert(value == 0);
  assert(watch::activityDistanceMillimetres(0) == 0);
}

void distanceUsesTheExistingStrideEstimate() {
  const auto data = watch::encodeActivityData(1000);
  const uint8_t expected_steps[] = {0xe8, 0x03, 0x00, 0x00};
  const uint8_t expected_distance[] = {0xf8, 0x65, 0x0b, 0, 0, 0, 0, 0};
  assert(std::memcmp(data.steps, expected_steps, sizeof(expected_steps)) == 0);
  assert(std::memcmp(data.distance, expected_distance, sizeof(expected_distance)) == 0);
  assert(watch::activityDistanceMillimetres(1000) == 747000);
  assert(readLittleEndian(data.distance, sizeof(data.distance)) == 747000);
}

void wireValuesUseLittleEndianInsteadOfHostStructureLayout() {
  const auto data = watch::encodeActivityData(0x12345678);
  const uint8_t expected_steps[] = {0x78, 0x56, 0x34, 0x12};
  const uint8_t expected_distance[] = {0x28, 0x50, 0xb8, 0x1e, 0x35, 0, 0, 0};
  assert(std::memcmp(data.steps, expected_steps, sizeof(expected_steps)) == 0);
  assert(std::memcmp(data.distance, expected_distance, sizeof(expected_distance)) == 0);
  assert(readLittleEndian(data.distance, sizeof(data.distance)) ==
         uint64_t(0x12345678) * 747);
}

void theFullStepCounterRangeDoesNotTruncateDistance() {
  const uint32_t maximum = std::numeric_limits<uint32_t>::max();
  const auto data = watch::encodeActivityData(maximum);
  assert(readLittleEndian(data.steps, sizeof(data.steps)) == maximum);
  const uint64_t expected = 3208340569365ULL;
  assert(watch::activityDistanceMillimetres(maximum) == expected);
  assert(readLittleEndian(data.distance, sizeof(data.distance)) == expected);
  assert(expected > std::numeric_limits<uint32_t>::max());
}

void standardPresentationDescriptorsMatchTheScalarFormats() {
  const uint8_t steps[] = {0x08, 0x00, 0x00, 0x27, 0x01, 0x00, 0x00};
  const uint8_t distance[] = {0x0a, 0xfd, 0x01, 0x27, 0x01, 0x00, 0x00};
  assert(watch::kActivityPresentationSize == sizeof(steps));
  assert(std::memcmp(watch::activityStepsPresentation(), steps, sizeof(steps)) == 0);
  assert(std::memcmp(watch::activityDistancePresentation(), distance, sizeof(distance)) == 0);
  assert(static_cast<int8_t>(watch::activityDistancePresentation()[1]) == -3);
}

void customUuidsAreDistinctAndStable() {
  assert(std::strcmp(watch::activityServiceUuid(),
                     "b44e0001-2cf5-4b4a-9f4d-8a6f4e7d2c10") == 0);
  assert(std::strcmp(watch::activityStepsUuid(),
                     "b44e0002-2cf5-4b4a-9f4d-8a6f4e7d2c10") == 0);
  assert(std::strcmp(watch::activityDistanceUuid(),
                     "b44e0003-2cf5-4b4a-9f4d-8a6f4e7d2c10") == 0);
}

}  // namespace

int main() {
  zeroStepsProduceZeroValues();
  distanceUsesTheExistingStrideEstimate();
  wireValuesUseLittleEndianInsteadOfHostStructureLayout();
  theFullStepCounterRangeDoesNotTruncateDistance();
  standardPresentationDescriptorsMatchTheScalarFormats();
  customUuidsAreDistinctAndStable();
  std::puts("Activity data: all 6 scenarios passed.");
}
