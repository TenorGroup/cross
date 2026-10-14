#pragma once
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
#include <Epub.h>

#include <memory>
#include <string>

#include "BookSearchMemory.h"
#include "activities/UiListActivity.h"

// Search in book, X4 Pro. Opened from the reader's More panel; shows the keyboard first (or the last results with
// the query's pill to type again), scans the chapters one pass at a time while it says which, then lists the hits:
// the chapter's name and the excerpt with the match in bold. A tap on a hit finishes with a ProgressChangeResult
// (chapter and visible text offset) for the reader to jump to; Back finishes cancelled.
class BookSearchActivity final : public UiListActivity {
 public:
  BookSearchActivity(GfxRenderer& renderer, MappedInputManager& input, std::shared_ptr<Epub> epub,
                     BookSearchMemory& memory);
  void onEnter() override;
  void loop() override;
  // A scan runs full speed and keeps the device awake; a chapter takes the lock whole, so Stop answers between chapters.
  bool skipLoopDelay() override { return state == State::Searching; }
  bool preventAutoSleep() override { return state == State::Searching; }

 private:
  enum class State : uint8_t { Typing, Searching, Results };

  int listCount() const override;
  const char* headerTitle() const override { return title.c_str(); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool pinMarkAfterName() const override { return true; }
  void drawPinMarks() override;
  bool handleCustomInput() override;
  bool handleButtons() override;

  size_t hitCount() const;
  // The row after the hits that says why the list ends (a full list, a stopped search), or null.
  const char* endNote() const;
  const std::string& chapterName(int spine) const;
  void openKeyboard(const std::string& initial);
  void onTyped(const ActivityResult& result);
  void beginSearch(const std::string& query);
  void stepSearch();
  void endSearch(bool stopped);
  void showResults();
  void leave();
  void setTitle();
  static void provideRow(void* context, uint16_t index, freeink::ui::ListItem& item);

  std::shared_ptr<Epub> epub;
  BookSearchMemory& memory;
  State state = State::Typing;
  bool leaving = false;
  int skipped = 0;
  uint32_t startedAt = 0;
  uint32_t lastPaint = 0;
  std::string title;
  std::string running, fullNote;
  mutable int namedSpine = -1;
  mutable std::string named;
};
#endif
