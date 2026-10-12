#define PHASE2_CHROME
#include "components/QuoteDetailLayout.h"
#include "components/QuoteListLayout.h"
#include "components/QuoteMarkGlyph.h"
#include "shells/ugly/UglyQuotePage.h"
#include <Icon.h>
#include "components/icons/tenorHomeTabIcons.h"
#include "components/icons/tenorReaderTabIcons.h"
#include "components/icons/tenorTextTabIcons.h"
namespace readermenu {
constexpr int X3_TAB_COUNT = 5, TAB_COUNT = 4;
// PHASE2_READER_ICONS
}
namespace fui = freeink::ui;
namespace tenorchrome {
int moreBelowChevronTopY(const GfxRenderer& renderer) { return renderer.getScreenHeight() - 70; }
}  // namespace tenorchrome
namespace x3readerchrome {
// PHASE2_PATH_FUNCTIONS
}
constexpr int BODY_FONT_ID = NOTOSERIF_16_FONT_ID;
constexpr int NUMBER_FONT_ID = UI_12_FONT_ID;
constexpr int DATE_FONT_ID = SMALL_FONT_ID;
constexpr int SOURCE_TITLE_FONT_ID = UI_12_FONT_ID;
constexpr int TITLE_FONT_ID = NOTOSERIF_12_FONT_ID;
constexpr int WORD_FONT_ID = NOTOSERIF_16_FONT_ID;
constexpr int UGLY_META_STEP = 32;
constexpr int FOOTER_GAP = 8, GLYPH_TOP_GAP = 56, HINTS_CLEARANCE = 16, CHEVRON_CLEARANCE = 6, TOP_GAP = 60;
int quotesHeaderBottom() { return 90; }
int statusBlockHeight(const GfxRenderer&) { return 200; }
void drawQuoteMark(const GfxRenderer&, int, int) {}
struct FileBrowserActivity : UiListActivity {
  enum class Mode { Books, PickFirmware };
  GfxRenderer& renderer;
  Mode mode = Mode::Books;
  std::vector<int> files{1};
  std::string searchQuery;
  bool favoriteSaveFailed = false, tooMany = false;
  explicit FileBrowserActivity(GfxRenderer& target) : UiListActivity(target), renderer(target) {}
  std::string screenTip();
  tenorchrome::X3ScreenLayout screenLayout();
  void drawX3Content();
};
struct EpubReaderBookmarksActivity {
  GfxRenderer& renderer;
  std::vector<int> bookmarks{1};
  explicit EpubReaderBookmarksActivity(GfxRenderer& target) : renderer(target) {}
  tenorchrome::X3ScreenLayout screenLayout() const;
  void drawX3Content() const;
};
struct EpubReaderChapterSelectionActivity {
  GfxRenderer& renderer;
  bool searchFailed = false, searching = false;
  int filterNumber = 0, count = 1;
  explicit EpubReaderChapterSelectionActivity(GfxRenderer& target) : renderer(target) {}
  int listCount() const { return count; }
  tenorchrome::X3ScreenLayout screenLayout() const;
  void drawX3Content() const;
};
struct XtcReaderChapterSelectionActivity {
  GfxRenderer& renderer;
  std::vector<int> rowItems{1};
  explicit XtcReaderChapterSelectionActivity(GfxRenderer& target) : renderer(target) {}
  tenorchrome::X3ScreenLayout screenLayout() const;
  void drawX3Content() const;
};
struct EpubReaderMenuActivity {
  using MenuTab = readermenu::Tab;
  GfxRenderer& renderer;
  MenuTab activeTabId = MenuTab::FAVORITES;
  int currentPage = 123, totalPages = 999, bookProgressPercent = 100;
  bool textOnly = false;
  explicit EpubReaderMenuActivity(GfxRenderer& target) : renderer(target) {}
  tenorchrome::X3ScreenLayout screenLayout() const;
  std::string progressText() const;
  bool infoOverBar() const;
  void drawX3Content() const;
  int tabCount() const;
  fui::BitmapRef tabIcon(int index, bool bold) const;
};
struct QuotesActivity {
  GfxRenderer& renderer;
  int page = 0, bookTotal = 512, quoteTotal = 512;
  bool books = true, oneBook = false;
  int count = 1;
  explicit QuotesActivity(GfxRenderer& target) : renderer(target) {}
  int pageCount() const { return 1; }
  int itemCount() const { return count; }
  bool showsBooks() const { return books; }
  bool bookLevel() const { return oneBook; }
  int placeFont() const { return SMALL_FONT_ID; }
  tenorchrome::X3ScreenLayout screenLayout() const;
  quotelist::Metrics metrics() const;
  std::string footer() const;
  void drawX3Content() const;
};
struct QuoteDetailActivity {
  GfxRenderer& renderer;
  struct {
    std::string title;
  } quote;
  std::vector<int> ids{1, 2};
  std::vector<std::string> titleLines, lines{"read this quote"};
  std::string chapter = "Chapter 1", when = "09/10/2026", pageLine = "Page 123";
  int index = 0, quotePage = 0, pages = 1, linesPerPage = 1, bodyPages = 1, metaOnLast = 0, metaPerPage = 1;
  bool loaded = true, size18 = false;
  // The page written in the pen has its own checks (UglyQuotePageTest, the simulator journeys); here it is a stub.
  ugly::quotepage::PageMap uglyMap;
  explicit QuoteDetailActivity(GfxRenderer& target) : renderer(target) {}
  void drawUglyPage() const {}
  bool penPage() const { return shell::uglyParts(); }
  tenorchrome::X3ScreenRect bodyArea() const { return screenLayout().bodyRect; }
  tenorchrome::X3ScreenLayout screenLayout() const;
  quotedetail::Metrics metrics() const;
  int visitX3Meta(bool paint, int firstLine = 0, int y = 0) const;
  int metaStep() const;
  void positionLabel(char* out, size_t size) const;
  void layoutX3Pages();
  void drawX3Content() const;
};
struct QuoteTrimActivity {
  GfxRenderer& renderer;
  std::vector<int> words = std::vector<int>(120);
  int first = 1, last = 110;
  bool movingEnd = true;
  explicit QuoteTrimActivity(GfxRenderer& target) : renderer(target) {}
  tenorchrome::X3ScreenLayout screenLayout() const;
  std::string screenTip() const;
  std::string screenInfo() const;
  void drawX3Content() const;
  int areaTop() const;
  int linesPerArea() const;
};
