#pragma once
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "UglyScreen.h"
#include "activities/home/HomeRows.h"

namespace ugly {

// Tier 3: a page of the notebook. One template for the five pages: a handwritten title with its
// underline, a sentence of abuse, then the rows with a pen circle on the chosen one. Left and Right turn
// to the page next door, Up and Down move the circle, a hold on Up or Down jumps a page of rows.
class Notebook final : public Screen {
 public:
  Notebook(GfxRenderer& renderer, MappedInputManager& mappedInput, homerows::Page page)
      : Screen("UglyNotebook", renderer, mappedInput), page(page), want(page) {}
  void onEnter() override;
  void render(RenderLock&&) override;

 protected:
  bool onKey(Key key) override;
  void afterKeys() override;

 private:
  // The Folder page keeps its names once, in `folder`; every other page keeps its row texts in `labels`.
  struct Rows {
    std::vector<std::string> labels, values, keys, folder;
    std::vector<RecentBook> books;
    std::vector<int> groups;
    bool tooMany = false;  // the root of the card holds more names than the heap allows: nothing is listed
    size_t cap = 0;
  };
  // Reads a page from the card. Takes no lock: it is slow, and the render task must not wait for it.
  Rows read(homerows::Page p) const;
  // Puts the rows read in place and keeps the cursor on a row. Needs no lock when no frame can be drawn
  // (onEnter); everywhere else the caller holds it.
  void adopt(Rows&& fresh);
  void reload();  // read, then adopt under the lock
  void activate(int row);
  int rowCount() const { return static_cast<int>(page == homerows::Page::Folder ? rows.folder.size() : rows.labels.size()); }
  const std::string& labelAt(int row) const { return page == homerows::Page::Folder ? rows.folder[row] : rows.labels[row]; }
  int rowsPerPage() const;
  int pagePosition(homerows::Page p) const;

  homerows::Page page;
  homerows::Page want;  // the page a turn is going to; `page` follows once its rows are read
  int cursor[homerows::PAGE_COUNT] = {};
  Rows rows;
};

}  // namespace ugly
