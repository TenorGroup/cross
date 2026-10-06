#pragma once
// A screen that hosts its own FreeInkUI app (UiAppHost), written by hand: the layout runs with its ink off and every
// text run it places is written in the pen of the shell where it stood. Boxes, bars and icons stay unpainted; the
// caller adds the marks of its selected row (uglychrome::marks on the row's published rect). UiListActivity keeps
// its own copy, with the marks of every row.
#include <FreeInkUIGfxRenderer.h>

#include <functional>

class GfxRenderer;

namespace ugly {

void layoutByHand(const GfxRenderer& renderer, freeink::ui::GfxRendererTarget& target,
                  const std::function<void()>& layout);

}  // namespace ugly
