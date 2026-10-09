int failures = 0;
void check(bool passed, const char* message) {
  if (passed) return;
  std::fprintf(stderr, "FAIL: %s\n", message);
  ++failures;
}

int main() {
#ifdef PHASE2_CHROME
  if (phase2ChromeTests()) return 1;
#endif
  int cases = 0;
  int textInfoCases = 0, textInfoMaxLines = 0;
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
        const int height = tenorchrome::tipHeight(renderer, "Giu OK: Ghim", 4);
        const auto home = tenorchrome::x3ScreenLayout(renderer, height);
        HomeActivity homePage(renderer);
        const auto actualHome = homePage.homeScreenLayout().barRect;
        check(actualHome.x == home.barRect.x && actualHome.y == home.barRect.y &&
                  actualHome.width == home.barRect.width && actualHome.height == home.barRect.height,
              "actual Home barRect equals reference");
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
#ifdef X3_SETTINGS_ADAPTER_AVAILABLE
        checkX3PathGeometry();
        checkSettingsScreenMatrix(renderer);
        UiScreen settingsScreen;
        check(beginScreen(settingsScreen, renderer, "Giu OK: Ghim"), "settings adapter enabled");
        check(settingsScreen.content.y == home.bodyRect.y &&
                  settingsScreen.content.bottom() == home.bodyRect.bottom(), "settings use Home body");
        GfxRenderer pathTarget;
        pathTarget.screenHeight = 792;
        drawPath(pathTarget, "Clock settings");
        check(pathTarget.ys.size() == 1 && pathTarget.ys[0] >= home.barRect.y &&
                  pathTarget.attemptedInkBottom <= home.barRect.bottom(), "path ink stays within Home bar");
        std::string longInfo;
        for (int line = 0; line < 80; ++line) longInfo += "Diagnostic line " + std::to_string(line) + "\n";
        const int pages = infoPageCount(renderer, longInfo.c_str(), home.bodyRect);
        check(pages > 1, "long information is paginated");
        int drawn = 0;
        for (int page = 0; page < pages; ++page) {
          GfxRenderer infoTarget;
          infoTarget.screenHeight = 792;
          drawInfoPage(infoTarget, longInfo.c_str(), home.bodyRect, page);
          drawn += infoTarget.paintedText.size();
          check(infoTarget.attemptedInkBottom <= home.bodyRect.bottom(), "information stays in body");
        }
        check(drawn == 80, "all information lines survive every tips/status mode");
        std::string edgeInfo;
        while (renderer.getTextWidth(SMALL_FONT_ID, (edgeInfo + "x").c_str()) <= home.bodyRect.width - 36)
          edgeInfo += "x";
        GfxRenderer edgeTarget;
        drawInfoPage(edgeTarget, edgeInfo.c_str(), home.bodyRect, 0);
        std::string recovered;
        for (const auto& line : edgeTarget.paintedText) recovered += line;
        check(recovered == edgeInfo, "paginated information must use the same wrap width as drawInfoInRect");
#endif
#ifdef X3_TEXT_TABS_AVAILABLE
        TextTabsBoundary textTabs;
        const TextTabsBoundary::Tab expectedTabs[] = {TextTabsBoundary::Tab::Family, TextTabsBoundary::Tab::Size,
            TextTabsBoundary::Tab::Layout, TextTabsBoundary::Tab::Style, TextTabsBoundary::Tab::Favorites};
        for (int index = 0; index < 5; ++index) {
          check(textTabs.tabForIndex(index) == expectedTabs[index], "Text tab IDs preserve legacy meanings");
          check(textTabs.displayIndex(expectedTabs[index]) == index, "Text descriptor IDs stay stable");
        }
        auto& pins = menucustom::state();
        pins = {};
        TextSettingsActivity textPage(renderer);
        textPage.tab_ = TextSettingsActivity::Tab::Favorites;
        textPage.rebuildRowItems();
        check(textPage.listCount() == 0, "Text Favorites starts empty without stored text pins");
        const char* pinKeys[] = {"settings/clock", "text/fontFamily", "unknown/key", "text/lineSpacing",
                                "text/dropCapMode", "text/fontSize"};
        for (const char* key : pinKeys) std::strcpy(pins.pins[pins.pinCount++].data(), key);
        textPage.refreshFavoriteRows();
        check(textPage.listCount() == 4 && textPage.rowItems_.size() == 4, "Text Favorites keeps only text routes");
        check(textPage.favoriteKey(0) == "text/fontFamily" && textPage.favoriteKey(3) == "text/fontSize",
              "Text Favorites preserves pin order and stable descriptor keys");
        std::strcpy(pins.pins[3].data(), "text/hyphenationEnabled");
        textPage.refreshFavoriteRows();
        check(textPage.favoriteKey(1) == "text/hyphenationEnabled" &&
                  textPage.rowItems_[1].label == tr(StrId::STR_HYPHENATION), "same-count pin changes rebuild labels");
        --pins.pinCount;
        textPage.refreshFavoriteRows();
        check(textPage.listCount() == 3, "unpinning shrinks the Text Favorites list");
        textPage.activateRow(1);
        check(textPage.tab_ == TextSettingsActivity::Tab::Style && textPage.activatedRow == 1 &&
                  textPage.activatedTab == TextSettingsActivity::Tab::Style,
              "Text Favorites activates the stable style descriptor after adding the leading tab");
        textPage.onTabAction(textPage.displayIndex(TextSettingsActivity::Tab::Favorites));
        textPage.activateRow(0);
        check(textPage.tab_ == TextSettingsActivity::Tab::Family && textPage.navigation.selected == 1,
              "Text font favorite opens the family tab without applying an arbitrary font");
        check(textPage.focusFavorite("settings/clock") == -1, "Text favorites reject routes from other screens");
        const auto savedPins = pins;
        for (auto tab : {TextSettingsActivity::Tab::Favorites, TextSettingsActivity::Tab::Family,
                         TextSettingsActivity::Tab::Size, TextSettingsActivity::Tab::Layout, TextSettingsActivity::Tab::Style}) {
          for (int state = 0; state < 3; ++state) {
            pins = state == 0 ? menucustom::State{} : savedPins;
            GfxRenderer textTarget;
            textTarget.screenHeight = 792;
            TextSettingsActivity textScreen(textTarget);
            textScreen.tab_ = tab;
            textScreen.rebuildRowItems();
            textScreen.favoriteSaveFailed = state == 1;
            textScreen.saveFailed_.value = state == 1;
            textScreen.ring = state == 2 && tab == TextSettingsActivity::Tab::Style ? 5 : 1;
            sdFontSystem.mask = state == 2 ? 1 : 15;
            textScreen.updatePreviewGeometry();
            UiScreen body;
            textScreen.prepareBody(body);
            check(body.body().y == 16 + textScreen.previewHeight, "actual Text preview and rows start at body top");
            const char* tip = textScreen.x3TipText();
            const auto placed = tenorchrome::x3ScreenLayout(textTarget, tip ? tenorchrome::tipHeight(textTarget, tip, 6) : 0);
            const int infoHeight = textScreen.screenInfo_.empty() ? 0
                                   : tenorchrome::tipHeight(textTarget, textScreen.screenInfo_.c_str(), 8) + 8;
            check(body.body().bottom() <= placed.bodyRect.bottom() - infoHeight,
                  "actual Text rows leave room for captions/errors");
            textScreen.drawX3Info();
            check(textTarget.attemptedInkBottom <= placed.barRect.y, "actual Text tips/captions stay above bar");
            const int infoLines = textScreen.screenInfo_.empty() ? 0
                                  : tenorchrome::tipLineCount(textTarget, textScreen.screenInfo_.c_str(), 8);
            check(textScreen.screenInfo_.empty() ||
                      infoLines == tenorchrome::tipLineCount(textTarget, textScreen.screenInfo_.c_str(), 1000),
                  "Text keeps every wrapped information line in the shipped translations");
            textInfoMaxLines = std::max(textInfoMaxLines, infoLines);
            ++textInfoCases;
            const int tipLines = tips ? tenorchrome::tipLineCount(textTarget, tip, 6) : 0;
            check(textTarget.paintedText.size() == infoLines + tipLines, "Text information survives tips Off");
            for (int index = 0; index < 5; ++index)
              check(static_cast<bool>(textScreen.tabIcon(index, false)) &&
                        textScreen.tabIcon(index, true).data != textScreen.tabIcon(index, false).data,
                    "all 5 Text slots carry regular/bold icons");
          }
        }
        sdFontSystem.mask = 15;
#endif
#ifdef X3_CLEARCACHEACTIVITY_AVAILABLE
        for (auto state : {ClearCacheActivity::WARNING, ClearCacheActivity::CLEARING,
                           ClearCacheActivity::SUCCESS, ClearCacheActivity::FAILED}) {
          GfxRenderer stateTarget;
          stateTarget.screenHeight = 792;
          ClearCacheActivity cache(stateTarget);
          cache.state = state;
          cache.renderX3();
          const auto stateBody = tenorchrome::x3ScreenLayout(stateTarget).bodyRect;
          auto countRect = stateBody;
          countRect.height = stateTarget.getLineHeight(SMALL_FONT_ID);
          const int expectedInfoLines = infoPageCount(stateTarget, cache.x3InfoText().c_str(), countRect) +
                                        (state == ClearCacheActivity::WARNING ? 2 : 0);
          check(stateTarget.paintedText.size() == expectedInfoLines + 1,
                "actual cache renderer keeps warnings/counts/results with tips Off");
          for (int top : stateTarget.ys)
            check(top >= home.barRect.y || top + stateTarget.getLineHeight(SMALL_FONT_ID) <= stateBody.bottom(),
                  "actual cache information stays above the bar");
          check(stateTarget.attemptedInkBottom <= home.barRect.bottom(), "cache path stays inside the bar");
        }
#endif
#ifdef X3_FONTDOWNLOADACTIVITY_AVAILABLE
        for (auto state : {FontDownloadActivity::LOADING_MANIFEST, FontDownloadActivity::DOWNLOADING,
                           FontDownloadActivity::COMPLETE, FontDownloadActivity::ERROR}) {
          GfxRenderer stateTarget;
          stateTarget.screenHeight = 792;
          FontDownloadActivity download(stateTarget);
          download.state_ = state;
          download.renderX3State();
          const auto stateBody = tenorchrome::x3ScreenLayout(stateTarget).bodyRect;
          auto countRect = stateBody;
          countRect.height = stateTarget.getLineHeight(SMALL_FONT_ID);
          const int expectedInfoLines = infoPageCount(stateTarget, download.x3InfoText().c_str(), countRect);
          const int retryLines = state == FontDownloadActivity::ERROR ? 1 : 0;
          check(stateTarget.paintedText.size() == expectedInfoLines + 1 + retryLines,
                "actual font renderer keeps file bytes/errors/install results with tips Off");
          check(std::count(stateTarget.paintedText.begin(), stateTarget.paintedText.end(), tr(STR_RETRY)) == retryLines,
                "actual font renderer keeps exactly one retry action in the error body");
          for (int top : stateTarget.ys)
            check(top >= home.barRect.y || top + stateTarget.getLineHeight(SMALL_FONT_ID) <= stateBody.bottom(),
                  "actual font information stays above the bar");
          check(stateTarget.attemptedInkBottom <= home.barRect.bottom(), "font path stays inside the bar");
          if (state == FontDownloadActivity::DOWNLOADING)
            check(GUI.progressBottom <= stateBody.bottom(), "actual font progress stays in body");
        }
#endif
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
#ifdef X3_SETTINGS_ADAPTER_AVAILABLE
  std::printf("%s: %d path glyph-center checks, shared font, height, padding and 8 px gaps\n",
              failures ? "RED" : "GREEN", pathGeometryCases);
  std::printf("GREEN: %d list recipes (14 classes x 18 combinations x 4 info states), 53 reachable rows each\n", settingsMatrixCases);
#endif
#ifdef X3_TEXT_TABS_AVAILABLE
  std::printf("GREEN: %d Text recipes, at most %d complete information lines\n", textInfoCases, textInfoMaxLines);
#endif
  return failures ? 1 : 0;
}
