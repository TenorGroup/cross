#pragma once
#include <array>

#include "ReadingStatsStore.h"
#include "activities/UiListActivity.h"
class BookStatsLibraryActivity final : public UiListActivity {
 public:
  BookStatsLibraryActivity(GfxRenderer& r, MappedInputManager& i) : UiListActivity("BookStatsLibrary", r, i) {}
  void onEnter() override;

 protected:
  const char* headerTitle() const override;
  int listCount() const override { return count; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void drawChrome() override;

 private:
  std::vector<ReadingStatsStore::BookEntry> books;
  // One recently visited page, released with this activity. The retained vector
  // and string capacity together must fit within 4 KiB.
  std::vector<ReadingStatsStore::BookEntry> adjacentBooks;
  uint32_t pageGeneration = 0;
  bool adjacentValid = false, adjacentBefore = false;
  bool adjacentPrevious = false, adjacentNext = false;
  std::array<freeink::ui::ListItem, 22> rows{};
  bool previous = false, next = false;
  int count = 0;
  void loadPage(const ReadingStatsStore::BookEntry& boundary, bool back);
};
