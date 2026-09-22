// Layout of the Quotes list: a bar, the quote in the reading font, a smaller source line.
// The numbers are checked here because on the panel a wrong inset only shows as "reads
// badly", which is what sent this screen back for a rework in the first place.
#include <cassert>
#include <cstdio>

#include "components/QuoteBlockLayout.h"

namespace {

// X3 panel, portrait, with the header and the button hints already taken off the band.
constexpr int16_t BAND_X = 0;
constexpr int16_t BAND_WIDTH = 528;
constexpr int16_t BAND_HEIGHT = 640;
// Noto Serif 16 and Be Vietnam Pro 10 as the generated fonts measure at 150 DPI.
constexpr int16_t QUOTE_LINE = 38;
constexpr int16_t SOURCE_LINE = 21;

quoteblock::Metrics metrics() { return {BAND_X, BAND_WIDTH, QUOTE_LINE, SOURCE_LINE}; }

}  // namespace

int main() {
  const auto m = metrics();

  // Brand floor: nothing sits closer than eight pixels to the edge of the band.
  const auto first = quoteblock::place(m, 0, 3, false);
  assert(first.barX - BAND_X >= 8);
  assert(BAND_X + BAND_WIDTH - (first.textX + first.textWidth) >= 8);
  assert(first.textWidth > 0);

  // Selecting a block only widens its bar: the text does not shift, so moving the cursor
  // never re-flows the page under the reader's eye.
  const auto selected = quoteblock::place(m, 0, 3, true);
  assert(selected.barX == first.barX);
  assert(selected.barWidth > first.barWidth);
  assert(selected.textX == first.textX);
  assert(selected.textWidth == first.textWidth);

  // Comfortable leading: the quote's lines stand further apart than the bare font does.
  assert(first.lineStep > QUOTE_LINE);

  // The source line sits under the last quote line, and the bar covers both.
  assert(first.sourceY >= first.textY + 3 * first.lineStep);
  assert(first.barHeight >= first.sourceY - first.barY + SOURCE_LINE);
  assert(first.height > first.barHeight);

  // A longer quote takes a taller block, one line at a time.
  for (int lines = 1; lines < quoteblock::MAX_LINES; ++lines) {
    assert(quoteblock::blockHeight(lines + 1, m) > quoteblock::blockHeight(lines, m));
    assert(quoteblock::blockHeight(lines + 1, m) - quoteblock::blockHeight(lines, m) == first.lineStep);
  }

  // Viewport. Six blocks of four lines each: a band of 640 pixels holds three of them.
  const uint8_t lines[6] = {4, 4, 4, 4, 4, 4};
  // The gap under the last visible block falls outside the band, so it does not count.
  assert(quoteblock::blockHeight(4, m) * 3 - quoteblock::BLOCK_GAP <= BAND_HEIGHT);
  assert(quoteblock::blockHeight(4, m) * 4 - quoteblock::BLOCK_GAP > BAND_HEIGHT);

  // A selection already on screen leaves the page alone.
  assert(quoteblock::followTop(lines, 6, 0, 0, BAND_HEIGHT, m) == 0);
  assert(quoteblock::followTop(lines, 6, 0, 2, BAND_HEIGHT, m) == 0);
  // One step past the last visible block scrolls by one block, not by a whole page.
  assert(quoteblock::followTop(lines, 6, 0, 3, BAND_HEIGHT, m) == 1);
  assert(quoteblock::followTop(lines, 6, 1, 4, BAND_HEIGHT, m) == 2);
  // Moving back up above the page pulls the page to the selection.
  assert(quoteblock::followTop(lines, 6, 3, 1, BAND_HEIGHT, m) == 1);

  // Mixed heights: two one-line quotes and three long ones share the page, so reaching
  // the last one drops both short blocks rather than a fixed number of rows.
  const uint8_t mixed[5] = {1, 1, 4, 4, 4};
  assert(quoteblock::followTop(mixed, 5, 0, 2, BAND_HEIGHT, m) == 0);
  assert(quoteblock::followTop(mixed, 5, 0, 4, BAND_HEIGHT, m) == 2);

  // A band too short for even one block still names the selected block, so the screen
  // draws something rather than nothing.
  assert(quoteblock::followTop(lines, 6, 0, 2, 10, m) == 2);
  assert(quoteblock::followTop(lines, 0, 0, 0, BAND_HEIGHT, m) == 0);

  puts("PASS: quote blocks keep the inset, the bar marks the selection, the page follows one block at a time");
}
