#pragma once
#include <array>

#include "QuoteStore.h"
#include "activities/UiListActivity.h"
class QuotesActivity final : public UiListActivity {
 public:
  QuotesActivity(GfxRenderer& r, MappedInputManager& input) : UiListActivity("Quotes", r, input) {}
  void onEnter() override;

 protected:
  int listCount() const override { return count; }
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;

 private:
  void loadPage(const std::string& boundary, bool previous);
  std::vector<std::string> names;
  std::array<freeink::ui::ListItem, quotes::PAGE_SIZE + 2> rows{};
  std::array<std::string, quotes::PAGE_SIZE + 2> labels;
  int count = 0;
  bool hasPrevious = false, hasNext = false;
};
