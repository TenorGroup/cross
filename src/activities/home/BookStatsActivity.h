#pragma once
#include <array>

#include "activities/UiListActivity.h"
class BookStatsActivity final : public UiListActivity {
 public:
  BookStatsActivity(GfxRenderer& r, MappedInputManager& input, std::string path, std::string title);
  void onEnter() override;

 protected:
  int listCount() const override { return rows.size(); }
  const char* headerTitle() const override { return title.c_str(); }
  void buildScreen(UiScreen& screen) override;
  void drawChrome() override;
  void activateIndex(int) override {}

 private:
  std::string path, title;
  std::array<freeink::ui::ListItem, 8> rows{};
  std::array<std::string, 8> values;
};
