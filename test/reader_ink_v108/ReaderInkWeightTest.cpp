#include <cassert>
#include <cstdint>
#include <initializer_list>
#ifdef READER_INK_V107_BASELINE
namespace readerInk {
constexpr uint8_t LEVEL_COUNT = 3;
constexpr uint8_t SCHEMA_VERSION = 0;
constexpr uint8_t clamp(int value) { return value >= 0 && value < 3 ? value : 0; }
constexpr uint8_t fromLegacy(int value) { return clamp(value); }
constexpr uint8_t physical(int value) { return clamp(value); }
constexpr uint8_t publicFromPhysical(int value) { return clamp(value); }
constexpr uint8_t publicMask(uint8_t mask) { return mask & 7; }
constexpr uint8_t next(int value) { return (clamp(value) + 1) % LEVEL_COUNT; }
constexpr bool available(int value, uint8_t mask) {
  return value >= 0 && value < LEVEL_COUNT && (mask & (1u << value));
}
constexpr uint8_t nextAvailable(int current, uint8_t mask) {
  for (uint8_t step = 1; step <= 3; ++step) {
    const auto candidate = static_cast<uint8_t>((current + step) % 3);
    if (mask & (1u << candidate)) return candidate;
  }
  return clamp(current);
}
}
#else
#include "ReaderInkWeight.h"
#endif
int main() {
  // Stored byte = level + 3: 0..2 are -3..-1, 3 is level "0", 4..8 are +1..+5.
  assert(readerInk::SCHEMA_VERSION == 3);
  assert(readerInk::LEVEL_COUNT == 9);
  assert(readerInk::ZERO == 3);
  // Old Strong must keep its appearance and become public +1 exactly once, in every older schema.
  assert(readerInk::fromLegacy(2) == 4);
  assert(readerInk::fromLegacy(1) == 4);
  assert(readerInk::fromLegacy(0) == 3);
  assert(readerInk::fromLegacy(3) == 3);
  assert(readerInk::fromLegacy(-1) == 3);
  assert(readerInk::fromSchema1(0) == 3);
  assert(readerInk::fromSchema1(1) == 4 && readerInk::fromSchema1(3) == 4);
  assert(readerInk::fromSchema1(4) == 3 && readerInk::fromSchema1(-1) == 3);
  for (int old = 0; old < 6; ++old) assert(readerInk::fromSchema2(old) == old + 3);
  assert(readerInk::fromSchema2(6) == 3 && readerInk::fromSchema2(255) == 3 && readerInk::fromSchema2(-1) == 3);
  constexpr uint8_t physical[] = {9, 8, 7, 0, 2, 3, 4, 5, 6};
  constexpr int32_t strength[] = {-48, -32, -16, 0, 16, 32, 48, 64, 96};
  for (int stored = 0; stored < 9; ++stored) {
    assert(readerInk::physical(stored) == physical[stored]);
    assert(readerInk::publicFromPhysical(physical[stored]) == stored);
    assert(readerInk::clamp(stored) == stored);
    assert(readerInk::level(stored) == stored - 3);
#ifndef READER_INK_V107_BASELINE
    assert(readerInk::outlineStrength(stored) == strength[stored]);
#endif
  }
  assert(readerInk::publicFromPhysical(1) == 3);
  assert(readerInk::publicFromPhysical(10) == 3);
  assert(readerInk::clamp(8) == 8);
  assert(readerInk::clamp(9) == 3);
  assert(readerInk::clamp(-1) == 3);
  assert(readerInk::clamp(255) == 3);
  assert(readerInk::publicFromPhysical(5) == 7);
  assert(readerInk::physical(255) == 0 && readerInk::physical(-1) == 0);
#ifndef READER_INK_V107_BASELINE
  assert(readerInk::outlineStrength(-1) == 0);
  assert(readerInk::outlineStrength(9) == 0);
#endif
  // Pack files to stored-level bits: base 3, weight-2..6 at 4..8, weight-7..9 at 2..0.
  assert(readerInk::publicMask(0x7f) == 0x1f8);
  assert(readerInk::publicMask(0x1f) == 0x078);
  assert(readerInk::publicMask(0x03) == 0x008);  // old Light is not new +1
  assert(readerInk::publicMask(0x05) == 0x018);  // old Strong is new +1
  assert(readerInk::publicMask(0x11) == 0x048);
  assert(readerInk::publicMask(0x381) == 0x00f);  // base and weight-7..9
  assert(readerInk::publicMask(0x080) == 0x004);  // weight-7 alone is -1
  assert(readerInk::publicMask(0x3ff) == 0x1ff);
  assert(readerInk::nextAvailable(3, 0x078) == 4);
  assert(readerInk::nextAvailable(4, 0x078) == 5);
  assert(readerInk::nextAvailable(5, 0x078) == 6);
  assert(readerInk::nextAvailable(6, 0x078) == 3);
  assert(readerInk::nextAvailable(3, 0x048) == 6);
  assert(readerInk::nextAvailable(6, 0x048) == 3);
  assert(readerInk::nextAvailable(3, 0x008) == 3);
  assert(readerInk::nextAvailable(255, 0x048) == 6);
  assert(readerInk::nextAvailable(0, 0x00f) == 1);
  assert(readerInk::nextAvailable(2, 0x00f) == 3);
  assert(readerInk::nextAvailable(3, 0x00f) == 0);

  // A short press always advances the requested level, -3 up to +5 and round to -3. Availability
  // controls the rendered fallback and warning, never which labels are reachable.
  for (const uint16_t mask : {uint16_t{0x018}, uint16_t{0x078}, uint16_t{0x1ff}}) {
    for (int stored = 0; stored < 8; ++stored) assert(readerInk::next(stored) == stored + 1);
    assert(readerInk::next(8) == 0);
    assert(readerInk::next(255) == 4);  // a corrupt byte is level 0, so the next press is +1
    assert(readerInk::available(3, mask));
    assert(readerInk::available(4, mask));
    assert(readerInk::available(5, mask) == (mask >= 0x078));
    assert(readerInk::available(0, mask) == (mask == 0x1ff));
    assert(!readerInk::available(9, mask) && !readerInk::available(-1, mask));
  }
}
