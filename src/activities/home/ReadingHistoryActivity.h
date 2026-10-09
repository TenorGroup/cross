#pragma once
#include <array>

#include "activities/UiListActivity.h"
#include "util/Lich30.h"
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
  // The arrows sit at y 195, beside the grid's third row, and read as "month before, month after" of a grid
  // that does not turn. Dropped while the grid is up: no ink, and the list still shows its more-below mark.
  bool showsSideArrows() const override { return !grid; }

 private:
  int gridTop() const;
  int gridHeight() const;
  int chartHeight() const;
  void drawGrid() const;
  void drawChart() const;
  bool grid = false;
  uint8_t cells[lich30::SO_O]{};
  uint32_t firstKey = 0, lastKey = 0;
  uint32_t weekMinutes[lich30::SO_TUAN]{}, weekKey[lich30::SO_TUAN]{};
  bool weekHasSubMinute[lich30::SO_TUAN]{};
  int count = 30;
  std::array<freeink::ui::ListItem, 30> rows{};
  std::array<std::string, 30> labels{}, values{}, subtitles{};
};
