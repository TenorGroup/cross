int failures = 0;
void check(bool passed, const char* message) {
  if (passed) return;
  std::fprintf(stderr, "FAIL: %s\n", message);
  ++failures;
}

int main() {
  check(tenorchrome::x3ScreenLayoutEnabled(), "ugly uses the shared X3 layout gate");
  int cases = 0;
  for (int tier = 0; tier < 3; ++tier) {
    SETTINGS.uiTextSize = tier;
    for (int mode = 0; mode < 3; ++mode) {
      SETTINGS.hidden = mode == 2;
      SETTINGS.large = mode == 1;
      for (bool tips : {false, true}) {
        SETTINGS.tipsEnabled = tips;
        GfxRenderer actual;
        actual.screenHeight = 792;
        const auto layout = tenorchrome::x3ScreenLayout(actual, tenorchrome::tipHeight(actual, "Giu OK: Ghim"));
        std::printf("layout tier=%d status=%d tips=%d bar=%d,%d,%d,%d body_bottom=%d tip=%d,%d\n", tier, mode,
                    tips, layout.barRect.x, layout.barRect.y, layout.barRect.width, layout.barRect.height,
                    layout.bodyRect.bottom(), layout.tipRect.y, layout.tipRect.height);
        ugly::pageHints(actual, "Trang truoc", "Trang sau", 792 - 52);
        GfxRenderer expected;
        expected.screenHeight = 792;
        const int baseline = layout.barRect.y + (layout.barRect.height + ugly::ascent(ugly::Size::S22)) / 2;
        ugly::mark(expected, ugly::Mark::Left, 32, baseline - 7);
        ugly::text(expected, ugly::Size::S22, 52, baseline, "Trang truoc");
        ugly::mark(expected, ugly::Mark::Right, 528 - 32, baseline - 7);
        ugly::text(expected, ugly::Size::S22, 528 - 52 - ugly::width(expected, ugly::Size::S22, "Trang sau"),
                   baseline, "Trang sau");
        check(actual.pixels == expected.pixels, "page hints match barRect at every pixel");
        check(actual.attemptedInkTop >= layout.barRect.y && actual.attemptedInkBottom <= layout.barRect.bottom(),
              "page hint ink stays in the shared bar");
        GfxRenderer path;
        path.screenHeight = 792;
        ugly::pathHints(path, "Settings/BLE", "3/5");
        GfxRenderer expectedPath;
        expectedPath.screenHeight = 792;
        ugly::text(expectedPath, ugly::Size::S22, 24, baseline, "Settings/BLE");
        ugly::text(expectedPath, ugly::Size::S22, 528 - 24 - ugly::width(expectedPath, ugly::Size::S22, "3/5"), baseline, "3/5");
        check(path.pixels == expectedPath.pixels, "handwritten path and count match the same bar at every pixel");
        GfxRenderer longPath;
        longPath.screenHeight = 792;
        const std::string route = std::string(80, 'a') + "/BLE";
        ugly::pathHints(longPath, route.c_str(), nullptr);
        const std::string tail = longPath.paintedText.empty() ? "" : longPath.paintedText.back();
        check(tail.size() >= 4 && tail.substr(tail.size() - 4) == "/BLE", "long paths keep the deepest name");
        check(ugly::width(longPath, ugly::Size::S22, tail.c_str()) <= layout.barRect.width - 48,
              "long handwritten path fits the shared bar");
        NotebookLayoutHarness rows;
        rows.renderer.screenHeight = 792;
        for (int first : {180, 210}) {
          const int rowCount = rows.rowsPerPage(first);
          for (int span : {100, 300, 450}) {
            GfxRenderer lastRow;
            lastRow.screenHeight = 792;
            const int lastBaseline = first + (rowCount - 1) * 52;
            ugly::circle(lastRow, ugly::Circle::Row, {48, lastBaseline - 26, 48 + span, lastBaseline + 8}, 12, 9, 7, 2);
            check(lastRow.attemptedInkBottom <= layout.bodyRect.bottom(), "actual notebook rings stay in bodyRect");
          }
        }
        GfxRenderer tip;
        tip.screenHeight = 792;
        tenorchrome::drawTip(tip, "Giu OK: Ghim");
        check(tip.paintedText.empty() == !tips, "ugly tip follows the tips switch");
        if (tips) {
          check(tip.attemptedInkTop >= layout.tipRect.y && tip.attemptedInkBottom <= layout.tipRect.bottom(),
                "actual ugly tip ink stays in tipRect");
          check(layout.bodyRect.bottom() + 8 <= tip.attemptedInkTop, "tip does not overlap body");
        }
        GfxRenderer info;
        info.screenHeight = 792;
        tenorchrome::drawInfoInRect(info, "BLE ready", layout.bodyRect);
        check(!info.paintedText.empty(), "information survives tips Off");
        JabHarness notebook;
        notebook.jab = "saved-value jab";
        const std::string firstJab = notebook.subtitle();
        SETTINGS.tipsEnabled = !tips;
        check(firstJab == notebook.subtitle(), "actual notebook jab stays unchanged when tips change");
        notebook.jab.clear();
        const std::string openingJab = notebook.subtitle();
        SETTINGS.tipsEnabled = tips;
        check(openingJab == notebook.subtitle(), "actual opening jab stays unchanged when tips change");
        for (int state = 0; state < 2; ++state) {
          BlePageTurnerActivity ble;
          ble.renderer.screenHeight = 792;
          ble.statusInTip_ = state == 0;
          ble.statusText_ = "BLE ready";
          if (state == 1) ble.bindNotice_ = "Key saved";
          ble.drawFooter();
          check(!ble.renderer.paintedText.empty(), "actual BLE status and bind result survive tips Off");
          if (!ble.renderer.paintedText.empty())
            check(ble.renderer.paintedText.back() == (state == 0 ? "ready" : "saved"),
                  "BLE information uses the handwritten branch");
          check(ble.renderer.attemptedInkBottom <= layout.barRect.y, "BLE information stays above the bar");
        }
        ++cases;
      }
    }
  }
  std::printf("%s: %d ugly layout cases, real shipped S22 glyphs\n", failures ? "RED" : "GREEN", cases);
  return failures ? 1 : 0;
}
