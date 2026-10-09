int failures = 0;
void check(bool passed, const char* message) {
  if (passed) return;
  std::fprintf(stderr, "FAIL: %s\n", message);
  ++failures;
}

int main() {
  int cases = 0;
  int activityCases = 0;
  std::map<std::string, std::vector<std::string>> phaseInformation;
  for (int tier = 0; tier < 3; ++tier) {
    SETTINGS.uiTextSize = tier;
    GfxRenderer renderer;
    renderer.screenHeight = 792;
    check(renderer.getLineHeight(SMALL_FONT_ID) == (tier == 0 ? 22 : tier == 1 ? 27 : 33),
          "caption uses shipped font advanceY");
    for (int mode = 0; mode < 3; ++mode) {
      SETTINGS.hidden = mode == 2;
      SETTINGS.large = mode == 1;
      for (bool tips : {false, true}) {
        SETTINGS.tipsEnabled = tips;
        {
          GfxRenderer target;
          target.screenHeight = 792;
          // PHASE_TWO_MATRIX
        }
        {
          GfxRenderer target;
          target.screenHeight = 792;
          const auto layout = tenorchrome::x3ScreenLayout(target);
          tenorchrome::phaseRings.clear();
          x3activity::drawBar(target, layout.barRect, "WiFi");
          check(tenorchrome::phaseRings.back().width == target.getTextWidth(UI_12_FONT_ID, "WiFi") + 28,
                "short breadcrumb fits its text");
          target.paintedText.clear();
          const std::string longPath(2048, 'x');
          x3activity::drawBar(target, layout.barRect, "leaf", longPath.c_str());
          check(tenorchrome::phaseRings.back().width <= layout.barRect.width - 48,
                "long breadcrumb stays inside bar");
          check(target.paintedText.back().size() >= 4 &&
                    target.paintedText.back().substr(target.paintedText.back().size() - 4) == "leaf",
                "long breadcrumb retains deepest component");
        }
        {
          GfxRenderer target;
          target.screenHeight = 792;
          const char* tip = "Hold OK to continue";
          const auto layout = tenorchrome::x3ScreenLayout(target, tenorchrome::tipHeight(target, tip));
          std::string information;
          std::vector<std::string> expected;
          for (int index = 0; index < 80; ++index) {
            const std::string line = "Information line " + std::to_string(index);
            expected.push_back(line);
            information += line + "\n";
          }
          x3activity::InfoPages page;
          std::vector<std::string> observed;
          page.draw(target, layout.bodyRect, {information.c_str()});
          check(page.pages > 1, "long information uses reachable pages");
          for (int index = 0; index < page.pages; ++index) {
            target.paintedText.clear();
            target.attemptedInkBottom = 0;
            page.draw(target, layout.bodyRect, {information.c_str()});
            observed.insert(observed.end(), target.paintedText.begin(), target.paintedText.end() - 1);
            check(target.attemptedInkBottom <= layout.bodyRect.bottom(), "paged information stays in body");
            check(page.turn(1), "next information page is reachable");
          }
          check(observed == expected, "all information lines survive paging and tips Off");
          check(page.page == 0, "information paging wraps");
          page.page = page.pages - 1;
          page.draw(target, layout.bodyRect, {"short status"});
          check(page.page == 0 && page.pages == 1, "shrinking information clamps the page");
          std::string token(1600, 'W');
          std::string recovered;
          x3activity::InfoPages::lines(target, token.c_str(), 480, [&](const char* line) { recovered += line; });
          check(recovered == token, "long unbroken tokens stay complete");
          std::string vietnamese = "Tiếng Việt có dấu.\nLỗi kết nối.\n";
          recovered.clear();
          x3activity::InfoPages::lines(target, vietnamese.c_str(), 480, [&](const char* line) { recovered += std::string(line) + "\n"; });
          check(recovered == vietnamese, "UTF-8 and explicit line breaks stay complete");
        }
        const int height = tenorchrome::tipHeight(renderer, "Giu OK: Ghim", 4);
        const auto home = tenorchrome::x3ScreenLayout(renderer, height);
        const auto textSettings = tenorchrome::x3ScreenLayout(renderer);
        check(home.bodyRect.y == 16, "body starts at 16");
        check(home.barRect.y == textSettings.barRect.y &&
                  home.barRect.height == textSettings.barRect.height,
              "Home and TextSettings bars have identical coordinates");
        check(home.barRect.height == (tier == 0 ? 60 : tier == 1 ? 67 : 72), "real tab height");
        check(home.bodyRect.bottom() + 8 == (tips ? home.tipRect.y : home.barRect.y), "body gap");
        check(home.tipRect.height == (tips ? height : 0), "tip reserve follows its switch");
        if (tips) check(home.tipRect.bottom() + 4 == home.barRect.y, "tip to bar gap");
        if (!tips) {
          std::printf("tier=%d mode=%d body=[%d,%d) bar=[%d,%d) buttons=%d\n", tier, mode,
                      home.bodyRect.y, home.bodyRect.bottom(), home.barRect.y, home.barRect.bottom(),
                      home.buttonBandRect.y);
        }
        if (SETTINGS.hidden) {
          check(home.barRect.bottom() == 792 - 16, "status Off keeps 16 px bottom inset");
        } else {
          check(home.barRect.bottom() + 4 == home.buttonBandRect.y, "bar to button band gap");
        }
        const int before = renderer.paintedText.size();
        tenorchrome::drawTipInRect(renderer, "Giu OK: Ghim", home.tipRect);
        check(renderer.paintedText.size() == before + (tips ? 1 : 0), "tip drawing follows its switch");
        const int infoBefore = renderer.paintedText.size();
        tenorchrome::drawInfoInRect(renderer, "File not found", home.bodyRect);
        check(renderer.paintedText.size() == infoBefore + 1, "information survives tips and status Off");
        ++cases;
        for (bool symbols : {false, true}) {
          SETTINGS.tenorButtonSymbols = symbols;
          GfxRenderer painted;
          painted.screenHeight = 792;
          const auto placed = tenorchrome::x3ScreenLayout(painted);
          painted.fillRect(placed.barRect.x, placed.barRect.y, placed.barRect.width, placed.barRect.height, true);
          TenorTheme{}.drawButtonHints(painted, "symbol", "symbol", "symbol", "symbol");
          bool intact = true;
          for (int row = placed.barRect.y; row < placed.barRect.bottom(); ++row)
            for (int column = 0; column < 528; ++column)
              intact = intact && painted.pixels[row * 528 + column];
          check(intact, "actual button footer must not erase the bar in text or symbol mode");
        }
        SETTINGS.tenorButtonSymbols = true;
        for (int state = 0; state < 6; ++state) {
          GfxRenderer target;
          target.screenHeight = 792;
          HomeActivity page(target);
          page.favoriteFileMissing = state == 0;
          page.activeTabId = state == 1 || state == 5 ? HomeActivity::Tab::STATS
                                       : state == 2 ? HomeActivity::Tab::FOLDER : HomeActivity::Tab::FAVORITES;
          if (state == 1) page.statsResetTip = StrId::STR_STATS_RESET_FAILED;
          if (state == 2) page.ring = 0;
          if (state == 3) page.rowItems.clear();
          if (state == 4) {
            page.favoriteSaveFailed = true;
            page.favoritesAvailable = false;
          }
          if (state == 5) page.statsResetTip = StrId::STR_STATS_RESET_PENDING;
          page.drawFooter();
          const char* info = page.homeInfoText();
          const int infoCount = info ? 1 : 0;
          const int tipCount = tips ? tenorchrome::tipLineCount(target, page.homeTipText(), 6) : 0;
          check(target.paintedText.size() == infoCount + tipCount, "actual Home footer keeps info and gates tips");
          if (info) check(target.paintedText.back() == info, "actual Home footer draws its error/result");
          check(target.attemptedInkBottom <= page.homeScreenLayout().barRect.y,
                "Home tip/info ink stays above the bar");
        }
      }
    }
  }
  SETTINGS.tipsEnabled = false;
  GfxRenderer off;
  tenorchrome::drawTip(off, "Giu OK: Ghim");
  check(off.paintedText.empty(), "shared drawTip respects tips Off");
  SETTINGS.tipsEnabled = true;
  SETTINGS.hidden = true;
  tenorchrome::drawTip(off, "Giu OK: Ghim");
  check(!off.paintedText.empty(), "status Off does not hide tips");
  std::printf("%s: %d layout cases with shipped caption fonts\n", failures ? "RED" : "GREEN", cases);
  std::printf("%s: %d activity layout combinations\n", failures ? "RED" : "GREEN", activityCases);
  return failures ? 1 : 0;
}
