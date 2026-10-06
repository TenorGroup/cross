#pragma once
// The list parts of tenor/ugly for the button readers: rows written in hand where a layout put them. UiListActivity
// draws every list with these; a screen that lays out its own list (Wi-Fi) calls row() for each of its rows.
#include <FreeInkUICore.h>

class GfxRenderer;

namespace uglychrome {

// Words where the layout put them: one line cut short by the scrawl, or wrapped when the layout lets them run to
// more lines and the box holds them. `locked` (a row that cannot be chosen now) strikes them through. Returns where
// the ink went (empty when nothing was written).
freeink::ui::Rect words(const GfxRenderer& renderer, const freeink::ui::Rect& rect, const char* text,
           freeink::ui::TextAlign align = freeink::ui::TextAlign::Left, bool locked = false, int maxLines = 1);

// The pen marks of a row laid out at `box`: the circle on the cursor row, a tick on the value in use, an arrow on a
// row that opens another screen, the word On or Off of a switch. The circle goes round the words in `around` (the
// label, as words() returned it), as the notebook circles its rows; with `around` empty it goes round the row.
struct Marks {
  bool selected = false, chosen = false, opensNext = false, toggle = false, toggleOn = false;
  freeink::ui::Rect around{};
};
void marks(const GfxRenderer& renderer, const freeink::ui::Rect& box, const Marks& marks);
// Room the marks take at the row's right end.
int marksWidth(const GfxRenderer& renderer, const Marks& marks);

// A whole row: the label, the subtitle under it, the value at the right, the group name above the row underlined,
// and the marks.
struct Row {
  const char* label = nullptr;
  const char* subtitle = nullptr;
  const char* value = nullptr;
  const char* heading = nullptr;
  bool locked = false;
  Marks marks;
};
void row(const GfxRenderer& renderer, const freeink::ui::Rect& box, const Row& row);

}  // namespace uglychrome
