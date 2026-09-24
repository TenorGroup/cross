#pragma once
#include <array>

#include "ReadingStatsStore.h"
#include "activities/UiListActivity.h"
class BookStatsActivity final : public UiListActivity {
 public:
  BookStatsActivity(GfxRenderer& r, MappedInputManager& input, std::string path, std::string title);
  void onEnter() override;
  // The expected finish (ngaydocxong::uocTinh on today's date) as the stats show it, here and on
  // the Recent card; false when there is too little reading to say.
  static bool finishText(const BookReadingRecord& b, char* text, size_t size);

 protected:
  int listCount() const override { return rows.size(); }
  const char* headerTitle() const override { return title.c_str(); }
  void buildScreen(UiScreen& screen) override;
  void drawChrome() override;
  void activateIndex(int) override {}

 private:
  std::string path, title;
  std::array<freeink::ui::ListItem, 9> rows{};
  std::array<std::string, 9> values;
};
