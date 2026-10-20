#pragma once

#include <cstdint>
class GfxRenderer;

// Reader text weight. The one place that decides how a level is stored.
//
// The user sees 9 levels, "-3" to "+5". settings.json and CrossPointSettings::readerInkWeight hold the
// level shifted up by ZERO, so the stored byte is never negative: 0..2 are -3..-1, 3 is level "0", 4..8 are
// +1..+5. Every function below takes and gives that stored byte unless it says level.
//
// A font pack names a variant by a physical number: 0 the base file, 1 the old Light file, 2..6 the
// files weight-2 to weight-6 (+1..+5), 7..9 the files weight-7 to weight-9 (-1..-3).
namespace readerInk {
void apply(GfxRenderer& renderer);
inline constexpr uint8_t SCHEMA_VERSION = 3;
inline constexpr uint8_t LEVEL_COUNT = 9;
inline constexpr uint8_t ZERO = 3;
inline constexpr uint8_t PACK_POSITIVE_LEVELS = 6;  // pack levels 0..5: the base file and +1..+5
// Outline strength per stored level, in 1/64 px. The 2-bit coverage rises in stairs where an edge crosses the
// 25/50/75% quantiser thresholds, so equal steps of 16 left some neighbours under 5% apart (BeVietnamPro 0 to +1
// at 14 pt: 4.5%). These values keep every pair of neighbours at least 5% apart in coverage for NotoSans,
// NotoSerif, BeVietnamPro and Literata from 12 to 22 pt (dev/dam/phu2bit, v1.0.69). Packs built with the earlier
// table (recipes v3-ramp and v4-thin) carry LEGACY_OUTLINE_STRENGTHS and still install.
inline constexpr int32_t OUTLINE_STRENGTHS[LEVEL_COUNT] = {-48, -32, -18, 0, 25, 37, 63, 81, 96};
inline constexpr int32_t LEGACY_OUTLINE_STRENGTHS[LEVEL_COUNT] = {-48, -32, -16, 0, 16, 32, 48, 64, 96};

constexpr uint8_t clamp(const int value) {
  return value >= 0 && value < LEVEL_COUNT ? static_cast<uint8_t>(value) : ZERO;
}

// Schema 0: the three old Light, Medium, Strong values. Old Strong is +1.
constexpr uint8_t fromLegacy(const int value) { return value == 1 || value == 2 ? ZERO + 1 : ZERO; }

// Schema 1 kept 1..3 for the old levels, all of them one step of +1 now.
constexpr uint8_t fromSchema1(const int value) { return value >= 1 && value <= 3 ? ZERO + 1 : ZERO; }

// Schema 2 stored 0..5 for the levels "0" to "+5".
constexpr uint8_t fromSchema2(const int value) {
  return value >= 0 && value < PACK_POSITIVE_LEVELS ? static_cast<uint8_t>(value + ZERO) : ZERO;
}

// The level the user reads, -3..+5.
constexpr int level(const int value) { return clamp(value) - ZERO; }

constexpr uint8_t physical(const int value) {
  const uint8_t stored = clamp(value);
  if (stored == ZERO) return 0;
  return stored > ZERO ? static_cast<uint8_t>(stored - 2) : static_cast<uint8_t>(9 - stored);
}

constexpr int32_t outlineStrength(const int value) { return OUTLINE_STRENGTHS[clamp(value)]; }
constexpr int32_t legacyOutlineStrength(const int value) { return LEGACY_OUTLINE_STRENGTHS[clamp(value)]; }

constexpr uint8_t publicFromPhysical(const int physicalLevel) {
  if (physicalLevel >= 2 && physicalLevel <= 6) return static_cast<uint8_t>(physicalLevel + 2);
  if (physicalLevel >= 7 && physicalLevel <= 9) return static_cast<uint8_t>(9 - physicalLevel);
  return ZERO;
}

// Physical bits 0, 2..6 and 7..9 as stored-level bits 3, 4..8 and 2..0.
constexpr uint16_t publicMask(const uint16_t physicalMask) {
  uint16_t mask = static_cast<uint16_t>((physicalMask & 1u) << ZERO);
  mask |= static_cast<uint16_t>((physicalMask & 0x7cu) << 2);
  for (uint8_t weight = 7; weight <= 9; ++weight)
    if (physicalMask & (1u << weight)) mask |= static_cast<uint16_t>(1u << (9 - weight));
  return mask;
}

constexpr uint8_t next(const int current) {
  return static_cast<uint8_t>((clamp(current) + 1) % LEVEL_COUNT);
}

constexpr bool available(const int value, const uint16_t mask) {
  return value >= 0 && value < LEVEL_COUNT && (mask & (1u << value));
}

constexpr uint8_t nextAvailable(const int current, const uint16_t mask) {
  const uint8_t stored = clamp(current);
  for (uint8_t step = 1; step <= LEVEL_COUNT; ++step) {
    const auto candidate = static_cast<uint8_t>((stored + step) % LEVEL_COUNT);
    if (mask & (1u << candidate)) return candidate;
  }
  return stored;
}
}  // namespace readerInk
