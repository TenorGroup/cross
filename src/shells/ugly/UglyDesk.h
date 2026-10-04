#pragma once
#include <string>

#include "RecentBooksStore.h"
#include "UglyScreen.h"

namespace ugly {

// Tier 2: the desk. A baked drawing of six objects, one per page of the notebook, the open book in
// the middle is the book being read. The circle sits on the chosen object; up and down go to the row above
// or below, sideways stays in the row and stops at its edge.
class Desk final : public Screen {
 public:
  Desk(GfxRenderer& renderer, MappedInputManager& mappedInput) : Screen("UglyDesk", renderer, mappedInput) {}
  void onEnter() override;
  void render(RenderLock&&) override;

 protected:
  bool onKey(Key key) override;

 private:
  // Objects by place on the desk: the buttons walk the picture (see logic::gridStep).
  enum Object : int { STATS, RECENT, READING, FOLDER, FAVORITES, SETTINGS, COUNT };
  void open();

  int selected = READING;
  int anchorX = -1;  // the x a sideways step ended on, so down and up again comes back (-1: the circle's own)
  bool hasBook = false;
  RecentBook book;
  int percent = 0;
};

}  // namespace ugly
