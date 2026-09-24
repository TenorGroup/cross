#pragma once
#include <cstdint>
#include <string>
#include <vector>
// SdCardFontSystem keeps one family by value (the one a wake loads from its memo).
struct SdCardFontFileInfo { uint8_t pointSize, style, stem; };
struct SdCardFontFamilyInfo { std::string name; std::vector<std::string> stems; std::vector<SdCardFontFileInfo> files; bool hiddenRoot = true; };
class SdCardFontRegistry { public: void discover() {} };
