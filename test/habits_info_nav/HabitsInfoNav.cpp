// Drives the production ReadingHabitsActivity::loop() (extracted by run.py) on the X3 layout.
#include <cstdio>

struct MappedInputManager {
  enum class Button { Back, Confirm, Left, Right, Up, Down };
  Button released = Button::Back;
  bool any = false;
  bool wasReleased(Button b) const { return any && b == released; }
};
namespace tenorchrome {
inline bool x3ScreenLayoutEnabled() { return true; }
}  // namespace tenorchrome
namespace x3activity {
#include "production_pages.inc"
}

struct UiListActivity {
  MappedInputManager mappedInput;
  int cursor = 0, updates = 0;
  // The list's own step: a released Right or Left moves the cursor one habit.
  void loop() {
    if (mappedInput.wasReleased(MappedInputManager::Button::Right)) ++cursor;
    if (mappedInput.wasReleased(MappedInputManager::Button::Left)) --cursor;
  }
  void requestUpdate() { ++updates; }
};
struct ReadingHabitsActivity : UiListActivity {
  x3activity::InfoPages x3Info;
  void loop();
};
#include "production_loop.inc"

int main() {
  int failures = 0;
  const auto press = [](ReadingHabitsActivity& a, MappedInputManager::Button b) {
    a.mappedInput.released = b;
    a.mappedInput.any = true;
    a.loop();
    a.mappedInput.any = false;
  };
  using B = MappedInputManager::Button;
  ReadingHabitsActivity a;
  a.x3Info.pages = 2;  // the first habit's explanation runs to 2 pages
  press(a, B::Right);
  if (a.cursor != 0 || a.x3Info.page != 1) {
    std::printf("FAIL first Right should turn to the explanation's page 2 (cursor=%d page=%d)\n", a.cursor, a.x3Info.page);
    ++failures;
  }
  press(a, B::Right);
  if (a.cursor != 1 || a.x3Info.page != 0) {
    std::printf("FAIL second Right should reach the next habit at its first page (cursor=%d page=%d)\n", a.cursor,
                a.x3Info.page);
    ++failures;
  }
  press(a, B::Left);
  if (a.cursor != 0 || a.x3Info.page != 0) {
    std::printf("FAIL Left on a first page should go back one habit (cursor=%d page=%d)\n", a.cursor, a.x3Info.page);
    ++failures;
  }
  ReadingHabitsActivity one;  // a 1-page explanation: every Right moves the cursor
  for (int i = 0; i < 3; ++i) press(one, B::Right);
  if (one.cursor != 3) {
    std::printf("FAIL a 1-page explanation should leave Right to the list (cursor=%d)\n", one.cursor);
    ++failures;
  }
  std::printf(failures ? "RED %d\n" : "GREEN\n", failures);
  return failures ? 1 : 0;
}
