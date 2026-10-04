#pragma once
#include "UglyScreen.h"

namespace ugly {

// The box Settings raises before the interface turns to tenor/ugly. Drawn in the pen of the shell itself, so the
// shock starts here. Two lines to choose from, the pen starts on the one that says no; Back says no too. The result is
// cancelled unless the first line was chosen.
class SwitchConfirm final : public Screen {
 public:
  SwitchConfirm(GfxRenderer& renderer, MappedInputManager& mappedInput) : Screen("UglySwitch", renderer, mappedInput) {}
  void onEnter() override;
  void render(RenderLock&&) override;
  // A box over Settings, not a Home: holding Back goes out to Home as everywhere else.
  bool isHomeActivity() const override { return false; }

 protected:
  bool onKey(Key key) override;

 private:
  enum Choice : int { YES, NO, COUNT };
  void answer(bool yes);
  int selected = NO;
};

}  // namespace ugly
