#pragma once
#include <Epub.h>

#include <memory>
#include <string>

#include "activities/UiListActivity.h"

class EpubReaderChapterSelectionActivity final : public UiListActivity {
  std::shared_ptr<Epub> epub;
  // The TOC entry being read, marked at the end of its row and where the list opens (a sub-chapter inside a spine
  // file counts: Section::getTocIndexForPage). -1: a page outside the table of contents (a cover, front matter)
  // marks no row and the list opens at the top.
  int currentTocIndex = -1;
  std::string info;          // where the reader stands (page of the chapter, share of the book), over the X3 bar

  // Windowed row buffers: TOC entries are SD-backed (BookMetadataCache LUT
  // reads), so only the rows around the viewport are materialized. A
  // several-hundred-entry TOC (547 in a large collection) built up front cost
  // ~60KB of labels + ListItems - starving the CJK glyph arena into
  // SD-per-repaint - for rows that were never drawn. The window follows
  // nav.top via itemsWindowFirst (see fui::ListProps); refreshing it also
  // batch-prewarms the window's fallback glyphs, so each page of the list
  // pays one bounded SD pass and repaints stay RAM-only.
  static constexpr int TOC_WINDOW = 24;
  std::string windowLabels[TOC_WINDOW];
  freeink::ui::ListItem windowItems[TOC_WINDOW];
  int windowStart = -1;
  int windowCount = 0;
  void refreshTocWindow(int start);

  uint32_t filterNumber = 0;
  int matches[TOC_WINDOW] = {};
  int matchCount = 0;
  int scanIndex = 0;
  int originalSelection = 0;
  uint32_t searchStarted = 0;
  bool searching = false;
  bool searchFailed = false;
  std::unique_ptr<BookMetadataCache::TocCursor> searchCursor;
  bool moreMatches = false;
  void enterNumber();
  void startSearch(bool nextBatch = false);
  void drawFooter() override;

  // Total TOC items count
  int listCount() const override {
    return !epub ? 0 : filterNumber ? (searching ? 0 : matchCount + (moreMatches ? 1 : 0)) : epub->getTocItemsCount();
  }
  void buildScreen(UiScreen& screen) override;
  bool ownsX3Chrome() const override { return true; }
  bool framedOnButtons() const override { return true; }
  tenorchrome::X3ScreenLayout screenLayout() const;
  bool infoOverBar() const;
  void drawX3Content() const;
  void activateIndex(int index) override;
  // Back cancels with a result and Confirm activates on RELEASE here, and a
  // missing epub swallows everything past Back.
  bool handleButtons() override;
  // Header is drawn inside the safe area (not full-width like the base).
  void drawChrome() override;

 public:
  tenorchrome::X3PathIcon navigationIcon() const override { return tenorchrome::X3PathIcon::Position; }
  std::string x3NavigationLabel() const override { return I18N.get(StrId::STR_SELECT_CHAPTER); }
  explicit EpubReaderChapterSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                              const std::shared_ptr<Epub>& epub, int currentTocIndex,
                                              std::string info);
  void onEnter() override;
  void loop() override;
  bool skipLoopDelay() override { return searching; }
  bool preventAutoSleep() override { return searching; }
};
