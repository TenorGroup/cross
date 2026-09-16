#pragma once
#include <array>

#include "activities/UiListActivity.h"
class ReadingHistoryActivity final : public UiListActivity {
 public:
  ReadingHistoryActivity(GfxRenderer& r, MappedInputManager& i) : UiListActivity("ReadingHistory", r, i) {}
  void onEnter() override;

 protected:
  const char* headerTitle() const override;
  int listCount() const override { return count; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int) override {}
  void drawChrome() override;

 private:
  int count = 30;
  std::array<freeink::ui::ListItem, 30> rows{};
  std::array<std::string, 30> labels{}, values{}, subtitles{};
};
