#pragma once
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "QuoteStore.h"
#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "components/QuoteDetailLayout.h"
#include "components/TenorMenuChrome.h"
#include "shells/ugly/UglyQuotePage.h"

// Bottom of the header the three Quotes screens share: where their content starts.
int quotesHeaderBottom();

// Text rules the Quotes screens share, one definition each.
namespace quotetext {
// `text` cut at `limit` bytes without splitting a codepoint.
std::string clipped(const std::string& text, size_t limit);
// A cut line ends "word …" when the cut falls just after a space; the ellipsis belongs on
// the word (mockup A1).
void tidyEllipsis(std::string& line);
// True when the words already end on a closing double quote, so a screen does not add a
// second one after it.
bool endsWithCloseQuote(const std::string& text);
}  // namespace quotetext

// One saved quote, read whole (mockup D2): the big opening mark, the words in Noto Serif 18
// or 16, then the book, chapter, moment and page. Left and Right walk the list in the
// order it was shown; the side buttons turn the pages of a quote too long for one screen.
// Select opens the options: edit (trim, or reselect on the page when the reader opened
// the list) and delete.
class Epub;

class QuoteDetailActivity final : public Activity {
 public:
  tenorchrome::X3PathIcon navigationIcon() const override { return tenorchrome::X3PathIcon::More; }
  std::string x3NavigationLabel() const override { return I18N.get(StrId::STR_QUOTES); }
  // `ids` is the list's current order and `index` the quote to open in it. The ids are
  // copied (8 bytes each) so a delete here can close the gap without asking the list.
  // `openBook`, when the reader opened the list, is its loaded book: chapter names of its
  // quotes come from it rather than from a second load of the same book.
  // What opens with the quote: nothing, or the Edit or Delete step a hold on the list's row asked for (touch).
  enum class Start : uint8_t { Quote, Edit, Delete };
  QuoteDetailActivity(GfxRenderer& r, MappedInputManager& input, std::vector<quotes::QuoteId> ids, int index,
                      bool insideReader, std::shared_ptr<Epub> openBook = nullptr, Start start = Start::Quote)
      : Activity("QuoteDetail", r, input),
        ids(std::move(ids)),
        index(index),
        insideReader(insideReader),
        openBook(std::move(openBook)),
        start(start) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  tenorchrome::X3ScreenLayout screenLayout() const;
  void drawX3Content() const;
  int visitX3Meta(bool paint, int firstLine = 0, int y = 0) const;
  int metaStep() const;
  void positionLabel(char* out, size_t size) const;
  void layoutX3Pages();
  // tenor/ugly on the button readers: the quote written by hand, broken into lines and pages in the pen's own widths.
  void layoutUglyBody();
  void drawUglyPage() const;
  // True when the page is written in the pen: tenor/ugly on the button readers (which draw the screen in the X3 layout) and on
  // the touch reader.
  bool penPage() const;
  // Where the page of the pen is laid out: the body rect of the screen, or on the touch reader what lies between its header and
  // the foot of the content.
  tenorchrome::X3ScreenRect bodyArea() const;

 private:
  enum class Menu : uint8_t { None, Options, Edit, Delete };

  // Loads quote `index` and lays it out. Callers hold the render lock.
  void show(int newIndex);
  // The chapter line of the quote being shown, fitted to `width`: worked out once per quote
  // and kept, since it may mean loading the book's table of contents from the card.
  std::string chapterFor(int width);
  void logShown() const;
  quotedetail::Metrics metrics() const;
  void openMenu(Menu which);
  void chooseFromMenu(Menu which, int choice);
  void openTrim();
  void deleteCurrent();
  void leave();
  void drawQuote();
  void drawDeleteConfirmation() const;

  std::vector<quotes::QuoteId> ids;
  int index = 0;
  bool insideReader = false;
  std::shared_ptr<Epub> openBook;
  // Chapter lines already worked out, by quote. Walking back to a quote, or showing it again
  // after a trim, reuses its line; the oldest goes first once CHAPTER_CACHE are held.
  static constexpr size_t CHAPTER_CACHE = 8;
  std::vector<std::pair<quotes::QuoteId, std::string>> chapters;

  QuoteRecord quote;
  bool loaded = false;
  std::vector<std::string> lines;
  std::vector<std::string> titleLines;
  std::string chapter;
  std::string when;
  std::string pageLine;
  bool size18 = true;
  int pages = 1;
  int quotePage = 0;
  int linesPerPage = 1;
  int bodyPages = 1;
  int metaOnLast = 0;
  int metaPerPage = 1;
  // tenor/ugly: the quote as the pen sets it (composed, with its closing mark) and where each page of it starts.
  std::string uglyBody;
  ugly::quotepage::PageMap uglyMap;

  OptionPopup popup;
  Menu menu = Menu::None;
  Start start = Start::Quote;
  // The quote before or after (-1, 1) and the pages of a long one: the front pair and the side buttons, or swipes.
  void stepQuote(int direction);
  void stepPage(int direction);
  int choice = -1;
  bool confirmingDelete = false;
  std::vector<std::string> excerpt;
  std::vector<std::string> note;
  const char* failure = nullptr;
  unsigned long failureAt = 0;
  // Something was deleted or trimmed, so the list reloads when this screen closes.
  bool changed = false;
};
