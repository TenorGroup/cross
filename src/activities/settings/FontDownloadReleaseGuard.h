#pragma once

#include <cstdint>

namespace fontdownload {

enum class ReleaseButton : uint8_t { None, Back, Confirm };

// A transition started by a press must consume that press's later release
// before the destination list can interpret it as another command.
class ReleaseConsumptionGuard {
 public:
  void arm(const ReleaseButton button) { button_ = button; }

  bool consumeIfReleased(const bool released, const bool stillPressed) {
    if (button_ == ReleaseButton::None) return false;
    if (released) {
      button_ = ReleaseButton::None;
      return true;
    }
    if (!stillPressed) button_ = ReleaseButton::None;
    return false;
  }

  ReleaseButton button() const { return button_; }
  bool active() const { return button_ != ReleaseButton::None; }

 private:
  ReleaseButton button_ = ReleaseButton::None;
};

}  // namespace fontdownload
