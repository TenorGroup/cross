#pragma once
#include <cstdint>

// Only the constant the font system sizes its variant tables by (the real block pulls in the page
// model); the value is the production one.
struct TextBlock {
  static constexpr uint8_t MAX_WORD_FONTS = 3;
};
