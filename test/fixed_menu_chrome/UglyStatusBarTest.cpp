int failures = 0;
void check(bool passed, const char* message) {
  if (passed) return;
  std::fprintf(stderr, "FAIL: %s\n", message);
  ++failures;
}

int main() {
  int cases = 0;
  const std::vector<void (*)(GfxRenderer&, const MappedInputManager&)> screens = {
      paintDiary, paintDesk, paintNotebook, paintQuestionSheet, paintFileBrowser};
  for (int tier = 0; tier < 3; ++tier) {
    SETTINGS.uiTextSize = tier;
    for (int mode = 0; mode < 3; ++mode) {
      SETTINGS.hidden = mode == 2;
      SETTINGS.large = mode == 1;
      for (bool valid : {false, true}) {
        clockstatus::valid = valid;
        for (bool percent : {false, true}) {
          SETTINGS.percent = percent;
          for (int clock = 0; clock < 3; ++clock) {
            SETTINGS.clockShowInHeader = clock;
            for (bool reverse : {false, true}) {
              MappedInputManager input;
              input.reverse = reverse;
              GfxRenderer root;
              root.screenHeight = 792;
              ugly::statusBar(root, input, {true, true, true, true});
              for (size_t screen = 0; screen < screens.size(); ++screen) {
                GfxRenderer child;
                child.screenHeight = 792;
                screens[screen](child, input);
                if (child.pixels != root.pixels) {
                  std::fprintf(stderr, "screen=%zu tier=%d mode=%d valid=%d percent=%d clock=%d reverse=%d\n",
                               screen, tier, mode, valid, percent, clock, reverse);
                  check(false, "all ugly screens share the same status pixels");
                }
              }
              const bool timePainted = std::find(root.paintedText.begin(), root.paintedText.end(), "09:28") != root.paintedText.end();
              check(timePainted == (!SETTINGS.hidden && valid), "all visible status bars show the valid clock");
              ++cases;
            }
          }
        }
      }
    }
  }
  SETTINGS.hidden = false;
  for (const char* reading : {"dang doc do", "đang đọc dở", "still reading", "chua doc gi"}) {
    GfxRenderer renderer;
    const auto label = ugly::deskLabelBox(renderer, ugly::PLACES[ugly::READING].box, reading, ugly::PLACES[4].box.x0 - 24);
    for (int object = 0; object < 6; ++object) {
      if (object == ugly::READING) continue;
      const auto box = ugly::PLACES[object].box;
      check(label.x1 <= box.x0 || label.x0 >= box.x1 || label.y1 <= box.y0 || label.y0 >= box.y1,
            "reading label stays clear of every other desk object");
    }
  }
  std::printf("%s: %d status cases, 5 screen routes, 4 desk labels\n", failures ? "RED" : "GREEN", cases);
  return failures ? 1 : 0;
}
