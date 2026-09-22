#pragma once

#include <Epub/Page.h>
#include <I18n.h>

#include <memory>
#include <string>
#include <vector>

#include "QuoteStore.h"
#include "WordSelectRepeat.h"
#include "activities/Activity.h"
#include "util/Dictionary.h"

// Word selection over the current reader page: Left/Right step through words
// in reading order, Up/Down jump rows, Confirm looks the word up and opens
// DictionaryDefinitionActivity, Back returns to the reader. On touch devices a
// touch-down moves the highlight and a tap on a word looks it up directly.
class DictionaryWordSelectActivity final : public Activity {
 public:
  explicit DictionaryWordSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                        std::unique_ptr<Page> page, int marginLeft, int marginTop)
      : Activity("DictionaryWordSelect", renderer, mappedInput),
        page(std::move(page)),
        marginLeft(marginLeft),
        marginTop(marginTop) {}

  void selectQuotation(QuoteRecord context) {
    quoteMode = true;
    quote = std::move(context);
    name = "QuoteSelect";
  }
  // Reselect a saved quote on the page it was kept on. The selector opens with the old range
  // marked, when it lies wholly on this page, so one Confirm keeps the quote as it was and
  // moving first saves a new range. Saving rewrites record `recordName` in place: the book,
  // title and moment it was kept stay, the words and their place come from the new range.
  // `spine` and `pageNumber` say where this page is, which the page itself does not carry.
  void editQuotation(QuoteRecord existing, std::string recordName, const int spine, const int pageNumber) {
    selectQuotation(std::move(existing));
    editName = std::move(recordName);
    editSpine = spine;
    editPage = pageNumber;
  }
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // Screen box of one selectable word. `text` points into the owned Page's
  // TextBlock arena (NUL-terminated), valid for this activity's lifetime.
  struct WordBox {
    int16_t x;
    int16_t y;
    int16_t width;
    uint16_t row;
    const char* text;
    EpdFontFamily::Style style;
  };

  enum class Popup : uint8_t { None, Busy, NotFound, Error, Saved };

  bool quoteMode = false;
  QuoteRecord quote;
  int anchor = -1;
  // Set by editQuotation(): the record Confirm rewrites instead of saving a new one.
  std::string editName;
  int editSpine = 0;
  int editPage = 0;
  // Marks the old range of the quote being edited, or queues the "not on this page" notice
  // and leaves a fresh selection.
  void preselectEditedRange();
  void confirmQuotation();
  void extractWords();
  int closestInRow(uint16_t row, int centerX) const;
  int wordAt(int x, int y) const;
  void moveVertical(int direction);
  void performLookup();
  bool drawHighlightWithSnapshot();
  void drawHints() const;
  void drawSavedPopup() const;

  std::unique_ptr<Page> page;
  const int marginLeft;
  const int marginTop;
  int fontId = 0;
  int lineHeight = 0;

  std::vector<WordBox> words;
  int selected = 0;
  uint16_t rowCount = 0;
  wordselect::Repeat horizontalRepeat;

  Dictionary dict;
  bool dictOpenAttempted = false;
  bool dictOpenOk = false;
  bool dictNeedsIndex = false;

  Popup popup = Popup::None;
  StrId popupMsg = StrId::STR_DICT_NOT_FOUND;
  unsigned long popupTime = 0;

  // Differential highlight repaint: the pixels under the current highlight
  // box, so a cursor move restores them and repaints only the two affected
  // boxes instead of re-running the full two-pass page render (which also
  // reloads every SD-font glyph on the page). snapshotIdx is the word whose
  // under-pixels are saved; -1 means the framebuffer no longer holds a clean
  // page (popup drawn, sub-activity shown) and the next render must be full.
  static constexpr size_t SNAPSHOT_CAPACITY = 4096;
  std::unique_ptr<uint8_t[]> snapshot;
  int16_t snapshotX = 0;
  int16_t snapshotY = 0;
  int16_t snapshotW = 0;
  int16_t snapshotH = 0;
  int snapshotIdx = -1;
};
