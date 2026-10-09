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
            for (int placement : {1, 2}) {
              SETTINGS.statusBarClock = placement;
              for (int format : {0, 1}) {
                SETTINGS.clockFormat = format;
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
              const bool timePainted = std::find(root.paintedText.begin(), root.paintedText.end(),
                                                format ? "12:59 PM" : "09:28") != root.paintedText.end();
              check(timePainted == (!SETTINGS.hidden && valid), "all visible status bars show the valid clock");
              ++cases;
            }
              }
            }
          }
        }
      }
    }
  }
  SETTINGS.hidden = false;
  int gapCases = 0;
  const auto inkBounds = [](const GfxRenderer& canvas) {
    std::array<int, 2> bounds{canvas.getScreenWidth(), -1};
    for (int row = canvas.getScreenHeight() - 60; row < canvas.getScreenHeight(); ++row)
      for (int column = 0; column < canvas.getScreenWidth(); ++column)
        if (canvas.pixels[row * canvas.getScreenWidth() + column]) {
          bounds[0] = std::min(bounds[0], column);
          bounds[1] = std::max(bounds[1], column);
        }
    return bounds;
  };
  clockstatus::valid = true;
  SETTINGS.clockFormat = 1;
  int longestWidth = 0;
  GfxRenderer measure;
  for (int hour = 1; hour <= 12; ++hour)
    for (int minute = 0; minute < 60; ++minute)
      for (const char* period : {"AM", "PM"}) {
        char clock[12];
        std::snprintf(clock, sizeof(clock), "%d:%02d %s", hour, minute, period);
        const int span = ugly::width(measure, ugly::Size::S22, clock);
        if (span > longestWidth) {
          longestWidth = span;
          halClock.twelve = clock;
        }
      }
  std::printf("ugly widest 12h clock=%s width=%d\n", halClock.twelve.c_str(), longestWidth);
  check(halClock.twelve == "12:55 AM" && longestWidth == 105,
        "normal S22 clock matches the real renderer per-letter advance measurement");
  for (int format : {0, 1}) {
  SETTINGS.clockFormat = format;
  for (int tier = 0; tier < 3; ++tier) {
    SETTINGS.uiTextSize = tier;
    for (bool large : {false, true}) {
      SETTINGS.large = large;
      for (int placement : {1, 2}) {
        SETTINGS.statusBarClock = placement;
        for (int headerClock : {1, 2}) {
          SETTINGS.clockShowInHeader = headerClock;
          for (bool percent : {false, true}) {
            SETTINGS.percent = percent;
            GfxRenderer bar;
            ugly::statusText.clear();
            ugly::statusMarks.clear();
            ugly::traceStatus = true;
            ugly::statusBar(bar, "b", "c", "u", "d");
            ugly::traceStatus = false;
            std::vector<std::array<int, 2>> bounds;
            for (const auto& run : ugly::statusText) {
              GfxRenderer layer;
              ugly::text(layer, ugly::Size::S22, run.left, run.baseline, run.value.c_str());
              bounds.push_back(inkBounds(layer));
            }
            for (const auto& run : ugly::statusMarks) {
              GfxRenderer layer;
              ugly::mark(layer, static_cast<ugly::Mark>(run[0]), run[1], run[2]);
              bounds.push_back(inkBounds(layer));
            }
            GfxRenderer battery;
            ugly::battery(battery, placement == 2 ? bar.getScreenWidth() - 56 : 14,
                          bar.getScreenHeight() - 20, 100, percent);
            bounds.push_back(inkBounds(battery));
            std::sort(bounds.begin(), bounds.end());
            int gap = 1000;
            for (size_t index = 1; index < bounds.size(); ++index)
              gap = std::min(gap, bounds[index][0] - bounds[index - 1][1] - 1);
            check(gap >= 6, "ugly adjacent painted ink regions leave at least 6 pixels");
            std::printf("ugly tier=%d large=%d left=%d date=%d percent=%d gap=%d\n",
                        tier, large, placement == 2, headerClock == 2, percent, gap);
            ++gapCases;
          }
        }
      }
    }
  }
  }
  clockstatus::valid = true;
  SETTINGS.clockShowInHeader = 0;
  SETTINGS.clockFormat = 0;
  SETTINGS.statusBarClock = CrossPointSettings::STATUS_BAR_CLOCK_LEFT;
  GfxRenderer swapped;
  ugly::statusBar(swapped, "", "", "", "");
  GfxRenderer expectedClock;
  ugly::text(expectedClock, ugly::Size::S22, 14, swapped.getScreenHeight() - 12, "09:28");
  for (int row = swapped.getScreenHeight() - 40; row < swapped.getScreenHeight(); ++row)
    for (int column = 0; column < 100; ++column)
      check(swapped.pixels[row * swapped.getScreenWidth() + column] ==
                expectedClock.pixels[row * swapped.getScreenWidth() + column],
            "clock-left setting paints the clock on the left");
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
  std::printf("%s: %d status cases, %d ink-gap cases, 5 screen routes, 4 desk labels\n",
              failures ? "RED" : "GREEN", cases, gapCases);
  return failures ? 1 : 0;
}
