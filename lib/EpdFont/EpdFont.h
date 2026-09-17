#pragma once
#include "EpdFontData.h"

class EpdFont {
  void getTextBounds(const char* string, int startX, int startY, int* minX, int* minY, int* maxX, int* maxY) const;

 public:
  // Incremental form of getTextBounds(). The state keeps the same cursor,
  // kerning and combining-mark bookkeeping as the full-string measurement so
  // callers can measure every UTF-8 prefix without rebuilding a temporary
  // string for each candidate.
  struct TextBoundsState {
    int minX = 0;
    int minY = 0;
    int maxX = 0;
    int maxY = 0;
    int startY = 0;
    int lastBaseX = 0;
    int lastBaseLeft = 0;
    int lastBaseWidth = 0;
    int lastBaseTop = 0;
    int32_t prevAdvanceFP = 0;
    uint32_t prevCp = 0;
  };

  const EpdFontData* data;
  explicit EpdFont(const EpdFontData* data) : data(data) {}
  ~EpdFont() = default;
  void getTextDimensions(const char* string, int* w, int* h) const;

  static TextBoundsState beginTextBounds(int startX = 0, int startY = 0);
  void appendTextBounds(TextBoundsState& state, uint32_t cp, const char*& text) const;
  static int textBoundsWidth(const TextBoundsState& state) { return state.maxX - state.minX; }

  const EpdGlyph* getGlyph(uint32_t cp) const;

  /// Returns true if this font covers `cp`: either via its in-RAM interval
  /// table or, for SD card fonts, via the coverageHandler that consults the
  /// full RAM-resident coverage index. Unlike getGlyph(), it never performs
  /// storage I/O and never falls back to the replacement glyph - it reports
  /// only what this font can render. Used by the CJK UI font fallback to
  /// decide whether a string needs to be routed to another font.
  bool hasCodepoint(uint32_t cp) const;

  /// Returns the kerning adjustment (4.4 fixed-point in pixels) between two codepoints.
  /// Returns 0 if no kerning data exists for the pair.
  int8_t getKerning(uint32_t leftCp, uint32_t rightCp) const;

  /// Returns the ligature codepoint for a pair, or 0 if no ligature exists.
  uint32_t getLigature(uint32_t leftCp, uint32_t rightCp) const;

  /// Greedily applies ligature substitutions starting from cp, consuming
  /// as many following codepoints from text as possible. Returns the
  /// (possibly substituted) codepoint; advances text past consumed chars.
  uint32_t applyLigatures(uint32_t cp, const char*& text) const;
};
