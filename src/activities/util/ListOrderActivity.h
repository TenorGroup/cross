#pragma once

#include <functional>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"

// Touch: puts a list in a new order (the Home cards, the Favourites), as the X3 does with held keys. The item being
// moved wears the ring; a tap on a row moves it there. "Done" in the bar keeps the order, "<" leaves it as it was.
class ListOrderActivity final : public UiListActivity {
 public:
  // `onDone` gets the new order as indices into `labels`.
  ListOrderActivity(GfxRenderer& renderer, MappedInputManager& input, std::vector<std::string> labels, int moving,
                    std::function<void(const std::vector<int>&)> onDone);
  void onEnter() override;

 private:
  std::vector<std::string> labels_;
  std::vector<int> order_;
  std::vector<freeink::ui::ListItem> rows_;
  int moving_;
  std::function<void(const std::vector<int>&)> onDone_;
  int listCount() const override { return static_cast<int>(order_.size()); }
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleCustomInput() override;
};
