#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "CrossPointSettings.h"

namespace {
int failures = 0;
void expect(const bool ok, const char* label, const float value) {
  if (!ok) {
    std::printf("FAIL %s (%.3f)\n", label, value);
    ++failures;
  }
}
bool near(const float a, const float b) { return std::fabs(a - b) < 0.001f; }
}  // namespace

// The reader's line spacing starts from the factor the font's pack was built for, and stays monotone across the
// levels (a board with the 1.30 and 1.60 levels included).
int main() {
  std::strncpy(SETTINGS.sdFontFamilyName, "Inter", sizeof(SETTINGS.sdFontFamilyName) - 1);

  SETTINGS.sdPackLineFactor = 0.0f;  // a font made elsewhere: the card default 1.00 as before
  SETTINGS.lineSpacing = readerSpacing::LEVEL_DEFAULT;
  expect(near(SETTINGS.getReaderLineCompression(), 1.00f), "no pack factor: card default", SETTINGS.getReaderLineCompression());

  SETTINGS.sdPackLineFactor = 1.25f;
  SETTINGS.lineSpacing = readerSpacing::LEVEL_DEFAULT;
  expect(near(SETTINGS.getReaderLineCompression(), 1.25f), "pack factor is the default level", SETTINGS.getReaderLineCompression());
  SETTINGS.lineSpacing = readerSpacing::VERY_NARROW;
  expect(near(SETTINGS.getReaderLineCompression(), 1.15f), "narrowest level steps down from the pack factor", SETTINGS.getReaderLineCompression());

  for (const float pack : {1.2f, 1.25f, 1.3f, 1.35f, 1.4f}) {
    SETTINGS.sdPackLineFactor = pack;
    float previous = 0.0f;
    // Narrowest to widest: the enum is not in that order.
    for (const uint8_t level : std::initializer_list<uint8_t>{readerSpacing::VERY_NARROW, readerSpacing::NARROW, readerSpacing::LEVEL_DEFAULT,
                                readerSpacing::WIDE, readerSpacing::VERY_WIDE, readerSpacing::LINE_EXTRA_WIDE,
                                readerSpacing::LINE_ULTRA_WIDE}) {
      SETTINGS.lineSpacing = level;
      const float value = SETTINGS.getReaderLineCompression();
      expect(value > previous, "levels grow with the pack factor", value);
      previous = value;
    }
  }

  SETTINGS.sdFontFamilyName[0] = '\0';  // a built-in face ignores the pack factor
  SETTINGS.sdPackLineFactor = 1.4f;
  SETTINGS.lineSpacing = readerSpacing::LEVEL_DEFAULT;
  expect(near(SETTINGS.getReaderLineCompression(), 0.95f), "built-in face keeps its own base", SETTINGS.getReaderLineCompression());
  std::printf(failures ? "line factor: %d failed\n" : "line factor: ok\n", failures);
  return failures ? 1 : 0;
}
