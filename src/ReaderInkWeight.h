#pragma once

#include <cstdint>
class GfxRenderer;

namespace readerInk {
void apply(GfxRenderer& renderer);
inline constexpr uint8_t SCHEMA_VERSION = 2;
inline constexpr uint8_t LEVEL_COUNT = 6;

constexpr uint8_t clamp(const int value) {
  return value >= 0 && value < LEVEL_COUNT ? static_cast<uint8_t>(value) : 0;
}

constexpr uint8_t fromLegacy(const int value) { return value == 1 || value == 2 ? 1 : 0; }

constexpr uint8_t physical(const int publicLevel) {
  const uint8_t level = clamp(publicLevel);
  return level ? static_cast<uint8_t>(level + 1) : 0;
}

constexpr int32_t outlineStrength(const int publicLevel) {
  return clamp(publicLevel) * 32;
}

constexpr uint8_t publicFromPhysical(const int physicalLevel) {
  return physicalLevel >= 2 && physicalLevel <= 4 ? static_cast<uint8_t>(physicalLevel - 1) : 0;
}

constexpr uint8_t publicMask(const uint8_t physicalMask) {
  return static_cast<uint8_t>((physicalMask & 1u) | ((physicalMask & 0x1cu) >> 1));
}

constexpr uint8_t next(const int current) {
  return static_cast<uint8_t>((clamp(current) + 1) % LEVEL_COUNT);
}

constexpr bool available(const int publicLevel, const uint8_t mask) {
  return publicLevel >= 0 && publicLevel < LEVEL_COUNT && (mask & (1u << publicLevel));
}

constexpr uint8_t nextAvailable(const int current, const uint8_t mask) {
  const uint8_t level = clamp(current);
  for (uint8_t step = 1; step <= LEVEL_COUNT; ++step) {
    const auto candidate = static_cast<uint8_t>((level + step) % LEVEL_COUNT);
    if (mask & (1u << candidate)) return candidate;
  }
  return level;
}
}  // namespace readerInk
