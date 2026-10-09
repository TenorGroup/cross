#pragma once

#include <functional>
#include <utility>

#include "MappedInputManager.h"
#include "activities/X3ActivityLayout.h"

namespace x3activity {
class TwoChoicePopup {
 public:
  void show(const char* title, const char* const* options, int count, int selected,
            std::function<void(int)> onSelect) {
    title_ = title;
    count_ = std::clamp(count, 1, 2);
    for (int index = 0; index < count_; ++index) options_[index] = options[index];
    selected_ = std::clamp(selected, 0, count_ - 1);
    onSelect_ = std::move(onSelect);
    information_.page = 0;
    active_ = true;
  }

  bool isActive() const { return active_; }

  void step(const int direction) {
    if (active_) selected_ = (selected_ + count_ + direction) % count_;
  }

  template <typename Update>
  bool handleInput(const MappedInputManager& input, Update requestUpdate) {
    if (!active_) return false;
    using Button = MappedInputManager::Button;
    const int pageDirection = input.wasReleased(Button::Left) ? -1 : input.wasReleased(Button::Right) ? 1 : 0;
    if (information_.turn(pageDirection)) {
      requestUpdate();
    } else if (input.wasReleased(Button::Up) || input.wasReleased(Button::Left)) {
      step(-1);
      requestUpdate();
    } else if (input.wasReleased(Button::Down) || input.wasReleased(Button::Right)) {
      step(1);
      requestUpdate();
    } else if (input.wasReleased(Button::Confirm)) {
      active_ = false;
      auto onSelect = std::move(onSelect_);
      if (onSelect) onSelect(selected_);
      requestUpdate();
    } else if (input.wasReleased(Button::Back)) {
      active_ = false;
      onSelect_ = nullptr;
      requestUpdate();
    }
    return true;
  }

  void draw(const GfxRenderer& renderer, tenorchrome::X3ScreenRect body) {
    if (!active_) return;
    const std::string first = std::string(selected_ == 0 ? "> " : "  ") + options_[0];
    const std::string second = count_ == 2 ? std::string(selected_ == 1 ? "> " : "  ") + options_[1] : "";
    information_.draw(renderer, body, {title_.c_str(), first.c_str(), second.c_str()});
  }

 private:
  std::string title_, options_[2];
  std::function<void(int)> onSelect_;
  InfoPages information_;
  int count_ = 0, selected_ = 0;
  bool active_ = false;
};
}
