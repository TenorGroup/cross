#pragma once
#include "shells/ugly/UglyMarkSeed.h"
#include <array>
#include <memory>

#include "activities/UiListActivity.h"
#include "util/LichDai.h"
// Long-term statistics: pages 1 to 3 are 12 weeks as 4 weeks by 7 weekdays, newest first; page 4 is reading by hour
// of the day. The side keys (X3) or a sideways swipe (touch) turn the page; the list under the drawing holds the page's
// days or hours.
class ReadingHistoryActivity final : public UiListActivity {
 public:
  tenorchrome::X3PathIcon navigationIcon() const override { return tenorchrome::X3PathIcon::Stats; }
  ReadingHistoryActivity(GfxRenderer& r, MappedInputManager& i) : UiListActivity("ReadingHistory", r, i) {}
  void onEnter() override;

 protected:
  const char* headerTitle() const override;
  int listCount() const override { return count; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int) override {}
  void drawChrome() override;
  // The side arrows turn the page here.
  bool showsSideArrows() const override { return ready; }
  bool handleCustomInput() override;

 private:
  mutable ugly::MarkState mark_;
  void showPage(int next);
  bool hourPage() const { return page == lichdai::TRANG_TUAN; }
  int gridTop() const;
  int gridHeight() const;
  int chartHeight() const;
  void drawPage() const;
  void drawGrid() const;
  void drawWeekChart() const;
  void drawHourChart() const;
  // Read from the card once, on entry; its own 1.4 KB block, so the screen object stays the size it was.
  std::unique_ptr<readinghistory::Snapshot> history;
  uint32_t today = 0;
  bool ready = false;
  int page = 0;
  uint8_t cells[lichdai::SO_O]{};
  int todayCell = -1;
  uint32_t firstKey = 0, lastKey = 0;
  lichdai::Tuan weeks[lichdai::SO_TUAN]{};
  int count = 1;
  std::array<freeink::ui::ListItem, lichdai::SO_O> rows{};
  std::array<std::string, lichdai::SO_O> labels{}, values{}, subtitles{};
};
