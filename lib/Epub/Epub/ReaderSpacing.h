#pragma once
#include <algorithm>
#include <cstdint>

// One five-value spacing level shared by the text-spacing rows. The ordinal IS
// the value persisted in settings.json, so the order below is a wire format:
// append, never reorder.
//
// Every factor here comes from the T1 measurement gate
// (t1/spacing-envelope.json, measured over the accepted 120-file / 432-style
// corpus at 12..26 pt and weights 0..2 on the X3 portrait frame, margin 5). The
// envelope also records which levels sit below the measured ink-collision
// bound: the two compressed line levels collide for every style, 1.10 for
// 269/432 and 1.20 for 84/432. That is the trade the labels promise - fewer
// pixels between lines, less ink clearance - so the levels ship with the
// measured warning rather than a guessed margin.
namespace readerSpacing {
enum Level : uint8_t {
  LEVEL_DEFAULT = 0,      // Mặc định
  VERY_NARROW = 1,  // Siêu hẹp
  NARROW = 2,       // Hẹp
  WIDE = 3,         // Rộng
  VERY_WIDE = 4,    // Siêu rộng
  LEVEL_COUNT = 5
};

// An untrusted ordinal - a hand-edited file, an older save, a caller's index -
// folds to the default level instead of indexing past the tables below.
constexpr uint8_t clampLevel(const int level) {
  return (level >= 0 && level < LEVEL_COUNT) ? static_cast<uint8_t>(level) : static_cast<uint8_t>(LEVEL_DEFAULT);
}

// Drop cap for the first paragraph of a chapter. The ordinal is persisted, so
// append only. Default is the measured smaller size; Large keeps the size this
// firmware shipped before the mode existed, so a migrated file that had the old
// boolean does not change how the book looks.
enum DropCapMode : uint8_t {
  DROP_CAP_OFF = 0,      // Tắt
  DROP_CAP_DEFAULT = 1,  // Mặc định
  DROP_CAP_LARGE = 2,    // Lớn
  DROP_CAP_MODE_COUNT = 3
};

constexpr uint8_t clampDropCapMode(const int mode) {
  return (mode >= 0 && mode < DROP_CAP_MODE_COUNT) ? static_cast<uint8_t>(mode) : static_cast<uint8_t>(DROP_CAP_DEFAULT);
}

// So khoang trang cua mot lan thut dau dong theo muc da luu (0 Tat, 1 Mac dinh, 2 Rong). Mot cho
// duy nhat: ban doc (ParsedText::resolveFirstLineIndent), ban TXT va pan xem truoc deu lay tu day,
// nen pan khong the ve mot kieu thut khac voi trang sach.
constexpr int indentSpaces(const uint8_t level) { return level == 2 ? 6 : level == 1 ? 3 : 0; }

// One height calculation for the parser and the settings preview, so the page
// and its preview can never disagree. Zero means the feature is off.
constexpr int dropCapHeight(const uint8_t mode, const int lineHeight) {
  if (mode == DROP_CAP_OFF) return 0;
  if (mode == DROP_CAP_LARGE) return lineHeight * 2 - 4;
  return lineHeight + lineHeight / 2;
}

// Line spacing multiplies the style's advanceY. GfxRenderer::getLineHeight()
// keeps its int(advanceY * factor + 0.5) rounding.
constexpr float lineFactor(const uint8_t level) {
  switch (level) {
    case VERY_NARROW:
      return 0.90f;
    case NARROW:
      return 0.95f;
    case WIDE:
      return 1.10f;
    case VERY_WIDE:
      return 1.20f;
    default:
      return 1.00f;
  }
}

// The step each level adds to a family's own base line factor. Keeping the base
// separate is what preserves the old per-family behaviour: the reader packs and
// Noto Serif have always laid out at 1.00, Noto Sans at 0.95, so Mặc định stays
// the same look in every family instead of silently loosening Noto Sans by five
// percent, and the four other levels step evenly around that base.
constexpr float lineFactorOffset(const uint8_t level) {
  switch (level) {
    case VERY_NARROW:
      return -0.10f;
    case NARROW:
      return -0.05f;
    case WIDE:
      return 0.10f;
    case VERY_WIDE:
      return 0.20f;
    default:
      return 0.0f;
  }
}

// Signed pixels added to every cursor-advancing advance. A combining mark is
// placed on its base by combiningMark::anchorOver and never advances by its own
// advanceX, so it never receives this delta. The value rides the same 12.4
// fixed-point sum as the advance and the kern, so the distinction between
// neighbouring levels survives the single toPixel() snap that follows.
constexpr int letterPixels(const uint8_t level) {
  switch (level) {
    case VERY_NARROW:
      return -2;
    case NARROW:
      return -1;
    case WIDE:
      return 1;
    case VERY_WIDE:
      return 2;
    default:
      return 0;
  }
}

// Multiplier applied to the advance of U+0020 alone: Mac dinh 1.0, Sieu hep
// 0.5, Hep 0.75, Rong 1.5, Sieu rong 2.0. Integer permille keeps the layout path
// off the FPU and truncates exactly like int(spacePx * factor) for the
// non-negative advances in play. Inter-letter advances are untouched, so a CJK
// line with no U+0020 keeps its glyph spacing, and a combining mark - which
// never advances by its own advanceX - never receives this delta.
constexpr int wordPermille(const uint8_t level) {
  switch (level) {
    case VERY_NARROW:
      return 500;
    case NARROW:
      return 750;
    case WIDE:
      return 1500;
    case VERY_WIDE:
      return 2000;
    default:
      return 1000;
  }
}

// Signed whole pixels added to a U+0020 advance: int(spacePx * factor) -
// spacePx, so LEVEL_DEFAULT resolves to exactly 0. The renderer re-enters this as 16x
// in the 12.4 fixed-point space advance, which leaves the single toPixel() snap
// with exactly the advance the levels were measured at.
constexpr int wordPixels(const uint8_t level, const int spacePx) {
  return (spacePx * wordPermille(level)) / 1000 - spacePx;
}

// Pixels added once after each paragraph. LEVEL_DEFAULT keeps the legacy clamp; the
// other levels are a straight fraction of the resolved line height:
// max(1, int(lineHeight * factor + 0.5)) with 0.05 / 0.1 / 0.5 / 1.0.
constexpr int paragraphGap(const uint8_t level, const int lineHeight) {
  if (level == LEVEL_DEFAULT) return std::clamp((lineHeight + 4) / 5, 2, 10);
  const int permille = level == VERY_NARROW ? 50 : level == NARROW ? 100 : level == WIDE ? 500 : 1000;
  return std::max(1, (lineHeight * permille + 500) / 1000);
}

// textSpacingVersion < 3 rows, by their three-value screen meaning. Line and
// letter shared one row (0 Tight, 1 Default, 2 Wide); paragraph spacing had its
// own (0 Default, 1 Large, 2 Larger). A file older than v2 folded paragraph
// spacing to the v2 meaning first - that fold needs to tell an absent key from
// a zero, so it lives in CrossPointSettings::fromJson next to the document.
constexpr uint8_t legacyLineLetterLevel(const uint8_t v2Ordinal) {
  return v2Ordinal == 0 ? NARROW : v2Ordinal == 1 ? LEVEL_DEFAULT : WIDE;
}
constexpr uint8_t legacyParagraphLevel(const uint8_t v2Ordinal) {
  return v2Ordinal == 0 ? LEVEL_DEFAULT : v2Ordinal == 1 ? WIDE : VERY_WIDE;
}
}  // namespace readerSpacing
