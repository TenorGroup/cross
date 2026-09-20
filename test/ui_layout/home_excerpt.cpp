#include <cassert>
#include <cstdio>
#include "components/HomeExcerptStyle.h"
#include <builtinFonts/geist_12_regular.h>
#include <builtinFonts/geist_14_regular.h>
#include <builtinFonts/geist_16_regular.h>
#include <builtinFonts/notoserif_12_italic.h>
#include <builtinFonts/notoserif_14_italic.h>
#include <builtinFonts/notoserif_16_italic.h>

bool contains(const EpdFontData& font, uint32_t cp) {
  for (uint32_t i = 0; i < font.intervalCount; ++i)
    if (font.intervals[i].first <= cp && cp <= font.intervals[i].last) return true;
  return false;
}

int main() {
  const EpdFontData* serif[] = {&notoserif_12_italic, &notoserif_14_italic, &notoserif_16_italic};
  const EpdFontData* ui[] = {&geist_12_regular, &geist_14_regular, &geist_16_regular};
  const char* quote = "阅读时间和最近读过的书";
  assert(!homeExcerptUsesUiFont("Những ngày bình yên. Quiet reading."));
  for (int tier = 0; tier < 3; ++tier) {
#ifdef LEGACY_EXCERPT
    const auto* selected = serif[tier];
#else
    const auto* selected = homeExcerptUsesUiFont(quote) ? ui[tier] : serif[tier];
#endif
    const auto* p = reinterpret_cast<const unsigned char*>(quote);
    while (*p) {
      const auto cp = utf8NextCodepoint(&p);
      assert(contains(*selected, cp));
    }
  }
  puts("PASS: CJK excerpt resolves actual generated glyph intervals in all three tiers; Latin keeps serif");
}
