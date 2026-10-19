#pragma once

#include <cstdint>
#include <cstring>

// tenor/ugly reads in Shantell Sans. Going in (and at every start while in), the reader font becomes Shantell
// when its family is on the card and the font is still the default one; going out gives the default back if the
// font is still the one the shell chose. The memo says what was given way: 0 nothing yet, 1 the built-in font,
// 2 the shipped card font, 3 the user's own font (left alone until the shell is left).
namespace uglyfont {

constexpr char FAMILY[] = "ShantellSans";
constexpr char SHIPPED[] = "Bokerlam";  // CrossPointSettings::sdFontFamilyName's default

template <size_t N>
void take(char (&name)[N], const bool builtinDefault, uint8_t& memo, const bool familyOnCard) {
  if (memo == 0) {
    const bool shipped = std::strcmp(name, SHIPPED) == 0;
    if (name[0] != '\0' ? !shipped : !builtinDefault) {
      memo = 3;
    } else if (familyOnCard) {
      memo = shipped ? 2 : 1;
      std::strncpy(name, FAMILY, N - 1);
      name[N - 1] = '\0';
    }
  } else if (memo < 3 && std::strcmp(name, FAMILY) != 0) {
    memo = 3;
  }
}

template <size_t N>
void give(char (&name)[N], uint8_t& memo) {
  if ((memo == 1 || memo == 2) && std::strcmp(name, FAMILY) == 0) {
    std::strncpy(name, memo == 1 ? "" : SHIPPED, N - 1);
    name[N - 1] = '\0';
  }
  memo = 0;
}

}  // namespace uglyfont
