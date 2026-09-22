#include "QuoteBlockLayout.h"

namespace quoteblock {
namespace {

int16_t lineStepOf(const Metrics& m) { return static_cast<int16_t>(m.quoteLineHeight + LINE_EXTRA); }

// Bar top to source-line bottom, without the air under the block.
int16_t inkHeight(const int lines, const Metrics& m) {
  return static_cast<int16_t>(lines * lineStepOf(m) + SOURCE_GAP + m.sourceLineHeight);
}

}  // namespace

int16_t blockHeight(const int lines, const Metrics& m) {
  return static_cast<int16_t>(inkHeight(lines, m) + BLOCK_GAP);
}

Block place(const Metrics& m, const int16_t y, const int lines, const bool selected) {
  Block block;
  block.lineStep = lineStepOf(m);
  block.barX = static_cast<int16_t>(m.bandX + SIDE_INSET);
  block.barY = y;
  block.barWidth = selected ? BAR_WIDTH_SELECTED : BAR_WIDTH;
  block.barHeight = inkHeight(lines, m);
  // The text is placed against the widest the bar ever gets, so selecting a block widens
  // the bar into its own air instead of pushing the quote sideways.
  block.textX = static_cast<int16_t>(block.barX + BAR_WIDTH_SELECTED + BAR_GAP);
  block.textY = y;
  block.textWidth = static_cast<int16_t>(m.bandX + m.bandWidth - SIDE_INSET - block.textX);
  block.sourceY = static_cast<int16_t>(y + lines * block.lineStep + SOURCE_GAP);
  block.height = blockHeight(lines, m);
  return block;
}

int followTop(const uint8_t* lines, const int count, const int top, const int selected, const int16_t bandHeight,
              const Metrics& m) {
  if (lines == nullptr || count <= 0) return 0;
  if (selected <= top) return selected < 0 ? 0 : selected;
  int first = top < 0 ? 0 : top;
  while (first < selected) {
    int used = 0;
    for (int i = first; i <= selected; i++) used += blockHeight(lines[i], m);
    // The gap under the last block hangs off the bottom of the band, so it is not part
    // of what has to fit.
    if (used - BLOCK_GAP <= bandHeight) break;
    first++;
  }
  return first;
}

}  // namespace quoteblock
