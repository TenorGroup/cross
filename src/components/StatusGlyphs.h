#pragma once
#include <cstdint>

// What the touch shell's status strip shows for the page-turn remote (dynamic bar rule 10): nothing
// while its radio is off, "B" while it links, the Bluetooth mark once linked, the mark struck through
// when a link dropped or none came within BT_LINK_GIVE_UP_MS.
namespace statusglyph {

enum class Bt : uint8_t { None, Linking, Linked, Lost };

// As long as a remote gets to link before the strip calls it lost.
constexpr unsigned long BT_LINK_GIVE_UP_MS = 30000;

constexpr Bt bluetooth(const bool radioOn, const bool linked, const bool wasLinked, const unsigned long linkingMs) {
  if (linked) return Bt::Linked;
  if (!radioOn) return Bt::None;
  if (wasLinked || linkingMs > BT_LINK_GIVE_UP_MS) return Bt::Lost;
  return Bt::Linking;
}

}  // namespace statusglyph
