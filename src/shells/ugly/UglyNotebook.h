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
      : Screen("UglyNotebook", renderer, mappedInput), page(page) {}
  void onEnter() override;
  void render(RenderLock&&) override;

 protected:
  bool onKey(Key key) override;

 private:
  struct Rows {
    std::vector<std::string> labels, values, keys, folder;
    std::vector<RecentBook> books;
    std::vector<int> groups;
  };
  void load();
  void activate(int row);
  void turn(int step);
  int rowsPerPage() const;
  int pagePosition() const;

  homerows::Page page;
  int cursor[homerows::PAGE_COUNT] = {};
  Rows rows;
};

}  // namespace ugly
