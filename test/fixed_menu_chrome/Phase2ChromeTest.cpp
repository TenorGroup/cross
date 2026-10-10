int phase2Failures = 0, phase2Cases = 0;
void phase2Check(bool valid, const char* name) {
  if (valid) return;
  ++phase2Failures;
  std::fprintf(stderr, "RED phase2: %s, tier=%d, status=%d/%d, tips=%d, language=%d\n", name, SETTINGS.uiTextSize,
               SETTINGS.hidden, SETTINGS.large, SETTINGS.tipsEnabled, phase2Language);
}

std::string compactWords(const std::string& text) {
  std::string result;
  for (const char byte : text)
    if (byte != ' ' && byte != '\n' && byte != '\r') result += byte;
  return result;
}

// A page whose progress line stands over the bar keeps that room even with tips off.
template <class Page>
auto holdsInfoOverBar(Page& page, int) -> decltype(page.infoOverBar()) { return page.infoOverBar(); }
template <class Page>
bool holdsInfoOverBar(Page&, long) { return false; }

template <class Page>
void checkPage(Page& page, GfxRenderer& renderer, const char* name, const std::vector<std::string>& information = {}) {
  renderer.paintedText.clear();
  renderer.ys.clear();
  renderer.textFontIds.clear();
  renderer.attemptedInkTop = 800;
  renderer.attemptedInkBottom = 0;
  const auto placed = page.screenLayout();
  const auto home = tenorchrome::x3ScreenLayout(renderer);
  phase2Check(placed.barRect.x == home.barRect.x && placed.barRect.y == home.barRect.y &&
                  placed.barRect.width == home.barRect.width && placed.barRect.height == home.barRect.height,
              name);
  phase2Check(placed.bodyRect.y == 16 && placed.bodyRect.bottom() <= placed.barRect.y - 8, name);
  const bool infoBar = holdsInfoOverBar(page, 0);
  phase2Check(!SETTINGS.tipsEnabled && !infoBar ? placed.tipRect.height == 0
                                                : placed.tipRect.height == 0 || placed.bodyRect.bottom() + 8 <= placed.tipRect.y,
              name);
  page.drawX3Content();
  phase2Check(renderer.attemptedInkBottom <= placed.barRect.y, name);
  for (size_t index = 0; index < renderer.ys.size(); ++index) {
    const int top = renderer.ys[index];
    const int bottom = top + renderer.getLineHeight(renderer.textFontIds[index]);
    phase2Check((top >= placed.bodyRect.y && bottom <= placed.bodyRect.bottom()) ||
                    ((SETTINGS.tipsEnabled || infoBar) && top >= placed.tipRect.y && bottom <= placed.tipRect.bottom()),
                name);
  }
  std::string painted;
  for (const auto& text : renderer.paintedText) painted += compactWords(text);
  for (const auto& info : information) {
    phase2Check(painted.find(compactWords(info)) != std::string::npos, name);
    if (painted.find(compactWords(info)) == std::string::npos)
      std::fprintf(stderr, "expected=[%s], painted=[%s]\n", compactWords(info).c_str(), painted.c_str());
  }
  ++phase2Cases;
}

int phase2ChromeTests() {
  for (phase2Language = 0; phase2Language < 3; ++phase2Language) {
    for (int tier = 0; tier < 3; ++tier) {
      SETTINGS.uiTextSize = tier;
      for (int mode = 0; mode < 3; ++mode) {
        SETTINGS.hidden = mode == 2;
        SETTINGS.large = mode == 1;
        for (bool tips : {false, true}) {
          SETTINGS.tipsEnabled = tips;
          GfxRenderer renderer;
          renderer.screenHeight = 792;
          FileBrowserActivity file(renderer);
          checkPage(file, renderer, "FileBrowser populated");
          if (tips) checkPage(file, renderer, "FileBrowser all hints", {file.screenTip()});
          if (!tips) phase2Check(renderer.paintedText.empty(), "File hints are all optional");
          file.favoriteSaveFailed = true;
          checkPage(file, renderer, "FileBrowser pin failure", {tr(STR_MENU_SAVE_FAILED)});
          file.files.clear();
          checkPage(file, renderer, "FileBrowser empty", {tr(STR_NO_FILES_FOUND), tr(STR_MENU_SAVE_FAILED)});
          file.favoriteSaveFailed = false;
          file.tooMany = true;
          checkPage(file, renderer, "FileBrowser degraded", {tr(STR_FOLDER_TOO_MANY)});
          file.tooMany = false;
          file.mode = FileBrowserActivity::Mode::PickFirmware;
          checkPage(file, renderer, "FileBrowser firmware", {tr(STR_NO_BIN_FILES)});

          EpubReaderBookmarksActivity bookmarks(renderer);
          checkPage(bookmarks, renderer, "Bookmarks populated");
          if (!tips) phase2Check(renderer.paintedText.empty(), "Bookmark hint is optional");
          bookmarks.bookmarks.clear();
          checkPage(bookmarks, renderer, "Bookmarks empty", {tr(STR_NO_BOOKMARKS)});

          EpubReaderChapterSelectionActivity chapter(renderer);
          checkPage(chapter, renderer, "EPUB chapters populated");
          chapter.count = 0;
          checkPage(chapter, renderer, "EPUB chapters empty", {tr(STR_NO_CHAPTERS)});
          chapter.searching = true;
          checkPage(chapter, renderer, "EPUB chapters scanning", {tr(STR_CHAPTER_SEARCHING)});
          chapter.searchFailed = true;
          checkPage(chapter, renderer, "EPUB chapters error", {tr(STR_ERROR_GENERAL_FAILURE)});
          chapter.searchFailed = chapter.searching = false;
          chapter.filterNumber = 123;
          checkPage(chapter, renderer, "EPUB chapters no result", {tr(STR_CHAPTER_NOT_FOUND)});

          XtcReaderChapterSelectionActivity xtc(renderer);
          checkPage(xtc, renderer, "XTC chapters populated");
          xtc.rowItems.clear();
          checkPage(xtc, renderer, "XTC chapters empty", {tr(STR_NO_CHAPTERS)});

          EpubReaderMenuActivity menu(renderer);
          phase2Check(menu.tabCount() == 5, "Reader menu exposes 5 root slots");
          for (int tab = 0; tab < 5; ++tab) {
            phase2Check(menu.tabIcon(tab, false).data != nullptr && menu.tabIcon(tab, true).data != nullptr,
                        "Every reader root tab uses an icon");
            menu.activeTabId = static_cast<EpubReaderMenuActivity::MenuTab>(tab);
            checkPage(menu, renderer, "Reader menu", {menu.progressText()});
          }
          menu.bookProgressPercent = -1;
          checkPage(menu, renderer, "Reader menu indexing", {menu.progressText()});
          phase2Check(menu.tabIcon(2, false).data != menu.tabIcon(4, false).data,
                      "Reading and Text have distinct icons");
          menu.textOnly = true;
          phase2Check(menu.tabCount() == 1 && menu.tabIcon(0, false).data == icon_tenor_text_font_40.bits,
                      "Text-only menu keeps its text root icon");

          QuotesActivity quotes(renderer);
          checkPage(quotes, renderer, "Quotes books count", {quotes.footer()});
          quotes.books = false;
          checkPage(quotes, renderer, "Quotes all count", {quotes.footer()});
          quotes.count = 0;
          checkPage(quotes, renderer, "Quotes empty", {tr(STR_QUOTES_EMPTY)});

          QuoteTrimActivity trim(renderer);
          checkPage(trim, renderer, "Quote trim", {trim.screenInfo()});
          if (tips) checkPage(trim, renderer, "Quote trim all hints", {trim.screenTip(), trim.screenInfo()});
          const int wordBottom = trim.areaTop() + trim.linesPerArea() * renderer.getLineHeight(WORD_FONT_ID);
          const auto trimBody = trim.screenLayout().bodyRect;
          phase2Check(
              wordBottom + 8 + tenorchrome::tipHeight(renderer, trim.screenInfo().c_str(), 4) <= trimBody.bottom(),
              "Trim words clear mandatory state and count");
          trim.movingEnd = false;
          checkPage(trim, renderer, "Quote trim other end", {trim.screenInfo()});

          QuoteDetailActivity detail(renderer);
          detail.quote.title = std::string(512, 'W');
          detail.chapter = std::string(400, 'W');
          detail.lines.assign(96, "long quote page");
          detail.layoutX3Pages();
          int bodyLines = 0;
          std::string metadata;
          for (detail.quotePage = 0; detail.quotePage < detail.pages; ++detail.quotePage) {
            checkPage(detail, renderer, "Quote detail paginated");
            for (size_t line = 1; line < renderer.paintedText.size(); ++line) {
              if (renderer.paintedText[line] == "long quote page")
                ++bodyLines;
              else
                metadata += renderer.paintedText[line];
            }
          }
          phase2Check(bodyLines == 96, "Detail pagination keeps all quote lines");
          phase2Check(compactWords(metadata) ==
                          compactWords(detail.quote.title + detail.chapter + detail.when + detail.pageLine),
                      "Detail pagination keeps every metadata byte");
          detail.loaded = false;
          detail.quotePage = 0;
          checkPage(detail, renderer, "Quote detail read error", {tr(STR_QUOTES_UNREADABLE)});

          renderer.paintedText.clear();
          const std::string longPath = "/sách/" + std::string(700, 'W') + "/cuối";
          const auto icon = icon_tenor_home_folder_40;
          activityManager.file = true;
          x3readerchrome::drawPath(renderer, tenorchrome::x3ScreenLayout(renderer).barRect, icon, longPath.c_str());
          activityManager.file = false;
          phase2Check(renderer.paintedText.size() == 1 && renderer.paintedText[0].substr(0, 3) == "…" &&
                          renderer.paintedText[0].find("/cuối") != std::string::npos,
                      "Path cuts left and preserves UTF8 leaf");
          phase2Check(renderer.getTextWidth(SMALL_FONT_ID, renderer.paintedText[0].c_str()) <= 416,
                      "File path fits long pill");
        }
      }
    }
  }
  phase2Language = -1;
  std::printf("%s: %d actual activity state/layout cases, 3 sizes x 2 tips x 3 status x 3 languages\n",
              phase2Failures ? "RED" : "GREEN", phase2Cases);
  return phase2Failures ? 1 : 0;
}
