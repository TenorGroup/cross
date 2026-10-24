#pragma once

#include <cstdint>

struct DictionarySettingsStub {
  // The reader's spacing levels as the layout engine takes them (CrossPointSettings::TextSpacingParams).
  struct TextSpacingParams {
    int8_t characterSpacing = 0;
    uint8_t wordSpacingPercent = 100;
    uint8_t paragraphIndentSpaces = 0;
    uint8_t extraParagraphSpacing = 0;
  };
  uint8_t paragraphAlignment = 0;
  bool hyphenationEnabled = false;
  bool focusReadingEnabled = false;
  uint8_t dropCapMode = 0;
  TextSpacingParams textSpacingParams() const { return {}; }
  int getReaderFontId() const { return 0; }
  float getReaderLineCompression() const { return 1.0f; }
};

inline DictionarySettingsStub SETTINGS;
