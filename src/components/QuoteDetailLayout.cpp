#include "QuoteDetailLayout.h"

namespace quotedetail {
namespace {

int clampPositive(const int value) { return value > 0 ? value : 0; }

}  // namespace

int16_t bodyX0(const Metrics& m) { return static_cast<int16_t>(m.bandX + BODY_X0); }

int16_t bodyX1(const Metrics& m) { return static_cast<int16_t>(m.bandX + m.bandWidth - BODY_RIGHT_INSET); }

int16_t glyphX(const Metrics& m) { return static_cast<int16_t>(bodyX0(m) - GLYPH_X_BLEED); }

int16_t glyphY(const Metrics& m) { return m.bandTop; }

int16_t bodyTop(const Metrics& m) { return static_cast<int16_t>(m.bandTop + m.glyphHeight + GLYPH_GAP); }

int16_t metaBlockHeight() {
  return static_cast<int16_t>(META_RULE_GAP + META_TITLE_GAP + META_LIST_GAP + 2 * META_LINE_STEP +
                              META_BOTTOM_MARGIN);
}

int linesPerPage(const int16_t lineHeight, const Metrics& m) {
  if (lineHeight <= 0) return 0;
  return clampPositive((m.bandBottom - bodyTop(m)) / lineHeight);
}

int linesPerLastPage(const int16_t lineHeight, const Metrics& m) {
  if (lineHeight <= 0) return 0;
  return clampPositive((m.bandBottom - bodyTop(m) - metaBlockHeight()) / lineHeight);
}

FontChoice chooseFont(const int lineCount18, const int lineCount16, const Metrics& m) {
  const int lastMax18 = linesPerLastPage(m.lineHeight18, m);
  if (lineCount18 > 0 && lineCount18 <= lastMax18) return {true, 1};

  const int lastMax16 = linesPerLastPage(m.lineHeight16, m);
  if (lineCount16 <= lastMax16) return {false, 1};

  // Even 16 overflows a single page: paginate. Every page but the last holds as many
  // lines as the full body area allows; the last one reserves room for the meta block.
  const int perPage = linesPerPage(m.lineHeight16, m) > 0 ? linesPerPage(m.lineHeight16, m) : 1;
  const int lastMax = lastMax16 > 0 ? lastMax16 : 1;
  const int remaining = lineCount16 - lastMax;
  const int pages = 1 + (remaining + perPage - 1) / perPage;
  return {false, pages};
}

MetaBlock metaBlock(const Metrics& m, const int16_t bodyBottomY) {
  MetaBlock block;
  block.x = bodyX0(m);
  block.ruleY = static_cast<int16_t>(bodyBottomY + META_RULE_GAP);
  block.titleY = static_cast<int16_t>(block.ruleY + META_TITLE_GAP);
  block.chapterY = static_cast<int16_t>(block.titleY + META_LIST_GAP);
  block.whenY = static_cast<int16_t>(block.chapterY + META_LINE_STEP);
  block.pageY = static_cast<int16_t>(block.whenY + META_LINE_STEP);
  return block;
}

int16_t headerPositionRight(const Metrics& m) {
  return static_cast<int16_t>(m.bandX + m.bandWidth - HEADER_RIGHT_INSET);
}

}  // namespace quotedetail
