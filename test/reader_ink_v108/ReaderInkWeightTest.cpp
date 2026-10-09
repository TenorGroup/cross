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
  // Old Strong must keep its appearance and become public +1 exactly once.
  assert(readerInk::fromLegacy(2) == 1);
  assert(readerInk::fromLegacy(1) == 1);
  assert(readerInk::fromLegacy(0) == 0);
  assert(readerInk::fromLegacy(3) == 0);
  assert(readerInk::fromLegacy(-1) == 0);
  assert(readerInk::SCHEMA_VERSION == 2);
  assert(readerInk::LEVEL_COUNT == 6);
  constexpr uint8_t physical[] = {0, 2, 3, 4, 5, 6};
  constexpr int32_t strength[] = {0, 16, 32, 48, 64, 96};
  for (int publicLevel = 0; publicLevel < 6; ++publicLevel) {
    assert(readerInk::physical(publicLevel) == physical[publicLevel]);
    assert(readerInk::publicFromPhysical(physical[publicLevel]) == publicLevel);
    assert(readerInk::clamp(publicLevel) == publicLevel);
#ifndef READER_INK_V107_BASELINE
    assert(readerInk::outlineStrength(publicLevel) == strength[publicLevel]);
#endif
  }
  assert(readerInk::publicFromPhysical(1) == 0);
  assert(readerInk::clamp(4) == 4);
  assert(readerInk::clamp(5) == 5);
  assert(readerInk::clamp(6) == 0);
  assert(readerInk::publicFromPhysical(5) == 4);
  assert(readerInk::clamp(255) == 0);
#ifndef READER_INK_V107_BASELINE
  assert(readerInk::outlineStrength(-1) == 0);
  assert(readerInk::outlineStrength(6) == 0);
#endif
  assert(readerInk::publicMask(0x7f) == 0x3f);
  assert(readerInk::publicMask(0x1f) == 0x0f);
  assert(readerInk::publicMask(0x03) == 0x01); // old Light is not new +1
  assert(readerInk::publicMask(0x05) == 0x03); // old Strong is new +1
  assert(readerInk::publicMask(0x11) == 0x09);
  assert(readerInk::nextAvailable(0, 0x0f) == 1);
  assert(readerInk::nextAvailable(1, 0x0f) == 2);
  assert(readerInk::nextAvailable(2, 0x0f) == 3);
  assert(readerInk::nextAvailable(3, 0x0f) == 0);
  assert(readerInk::nextAvailable(0, 0x09) == 3);
  assert(readerInk::nextAvailable(3, 0x09) == 0);
  assert(readerInk::nextAvailable(0, 0x01) == 0);
  assert(readerInk::nextAvailable(255, 0x09) == 3);

  // A short press always advances the requested public level. Availability
  // controls the rendered fallback and warning, never which labels are reachable.
  for (const uint8_t mask : {uint8_t{0x03}, uint8_t{0x0f}}) {
    assert(readerInk::next(0) == 1);
    assert(readerInk::next(1) == 2);
    assert(readerInk::next(2) == 3);
    assert(readerInk::next(3) == 4);
    assert(readerInk::next(4) == 5);
    assert(readerInk::next(5) == 0);
    assert(readerInk::available(0, mask));
    assert(readerInk::available(1, mask));
    assert(readerInk::available(2, mask) == (mask == 0x0f));
    assert(readerInk::available(3, mask) == (mask == 0x0f));
  }
}
