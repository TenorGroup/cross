#pragma once
#include "UglyScreen.h"

namespace ugly {

// The question box of the shell, for every screen that asks yes or no: a sheet of paper laid down crooked, the
// question in a large hand, a note under it in a small one (may be empty), the 2 answers one under another and the
// circle on `selected`. Clears the screen first; draws the key bar but leaves the refresh to the caller. `drawn`
// gets where the 2 answers stand. Returns the paper's frame.
Box askBox(const GfxRenderer& renderer, const MappedInputManager& input, const char* question, const char* note,
           const char* const answers[2], int selected, Box drawn[2]);

// The box Settings raises before the interface turns to tenor/ugly. Drawn in the pen of the shell itself, so the
// shock starts here. Two lines to choose from, the pen starts on the one that says no; Back says no too. The result is
// cancelled unless the first line was chosen.
class SwitchConfirm final : public Screen {
 public:
  SwitchConfirm(GfxRenderer& renderer, MappedInputManager& mappedInput, bool toCross = false)
      : Screen("UglySwitch", renderer, mappedInput), toCross(toCross) {}
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
  const bool toCross;
  std::string crossNote;
#if FREEINK_DEVICE_X4PRO
  Box drawn[COUNT] = {};  // the two lines as last drawn, for the finger: a tap on one answers it
#endif
};

}  // namespace ugly
