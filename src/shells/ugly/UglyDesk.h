#pragma once
#include <string>

#include "RecentBooksStore.h"
#include "UglyScreen.h"

namespace ugly {

// Tier 2: the desk. A baked drawing of six objects, one per page of the notebook, the open book in
// the middle is the book being read. The circle sits on the chosen object.
class Desk final : public Screen {
 public:
  Desk(GfxRenderer& renderer, MappedInputManager& mappedInput) : Screen("UglyDesk", renderer, mappedInput) {}
  void onEnter() override;
  void render(RenderLock&&) override;

 protected:
  bool onKey(Key key) override;

 private:
  // The order the buttons walk the objects, round and round.
  enum Object : int { STATS, RECENT, READING, FOLDER, FAVORITES, SETTINGS, COUNT };
  void open();

  int selected = READING;
  bool hasBook = false;
  RecentBook book;
  int percent = 0;
};

}  // namespace ugly
