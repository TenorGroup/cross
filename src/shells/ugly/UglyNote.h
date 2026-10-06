#pragma once
// A note page of tenor/ugly on a button device: the paper of the notebook, the screen's name written and
// underlined, one sentence in the middle, a line of data under it, a progress bar drawn by pen, the status bar.
// The screens that wait on the network (clock sync, Wi-Fi, updates, fonts, the hotspot) say where they are with it,
// when shell::uglyParts() says so.
#include <HalDisplay.h>

#include "UglyInk.h"

namespace ugly {

// The first strokes of every note page: a clean buffer, the notebook's margin, the screen's name underlined.
// The text of a page starts at NOTE_X.
inline constexpr int NOTE_X = 48;
void notePaper(const GfxRenderer& renderer, const char* title);

// Draws the whole frame and pushes it. `line` is written by hand; `detail` is data (a time, a network, an address,
// a version) and stays one line in the UI font, where a letter cannot jump. Either may be null; `percent` below 0
// draws no bar.
void notePage(const GfxRenderer& renderer, const MappedInputManager& input, const char* title, const char* line,
              const char* detail, int percent, Hints hints,
              HalDisplay::RefreshMode mode = HalDisplay::FAST_REFRESH);

}  // namespace ugly
