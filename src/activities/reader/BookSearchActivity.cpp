#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
#include "BookSearchActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>

#include "MappedInputManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/BookSearchLayout.h"
#include "components/HeaderBackTapTarget.h"
#include "components/TenorMenuChrome.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
constexpr int ROW_HEIGHT = 76;
constexpr int NAME_FONT = SMALL_FONT_ID;
constexpr int TEXT_FONT = UI_10_FONT_ID;
constexpr int ROW_SIDE = 16;
constexpr int ROW_LINE_GAP = 4;
// The Telex keyboard's field, in letters.
constexpr size_t QUERY_LETTERS = 48;
// The scan's own screen refreshes (one e-ink refresh each), not more often than this.
constexpr uint32_t PAINT_EVERY_MS = 1200;

std::string trimmed(const std::string& text) {
  const auto first = text.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return {};
  return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}
}  // namespace

BookSearchActivity::BookSearchActivity(GfxRenderer& renderer, MappedInputManager& input, std::shared_ptr<Epub> epub,
                                       BookSearchMemory& memory)
    : UiListActivity("BookSearch", renderer, input), epub(std::move(epub)), memory(memory) {}

size_t BookSearchActivity::hitCount() const {
  const auto* results = memory.runner.results();
  return state == State::Results && results ? results->count : 0;
}

const char* BookSearchActivity::endNote() const {
  const auto* results = memory.runner.results();
  if (state != State::Results || !results || !results->count) return nullptr;
  if (results->full()) return fullNote.c_str();
  return memory.incomplete ? tr(STR_SEARCH_INCOMPLETE) : nullptr;
}

int BookSearchActivity::listCount() const { return static_cast<int>(hitCount() + (endNote() ? 1 : 0)); }

void BookSearchActivity::setTitle() {
  char text[160];
  snprintf(text, sizeof(text), tr(STR_BOOK_SEARCH_QUERY), memory.query.c_str());
  title = memory.query.empty() ? tr(STR_BOOK_SEARCH) : text;
}

void BookSearchActivity::onEnter() {
  UiListActivity::onEnter();
  title = tr(STR_BOOK_SEARCH);
  if (memory.hasResults()) {
    showResults();
    return;
  }
  openKeyboard("");
}

void BookSearchActivity::openKeyboard(const std::string& initial) {
  state = State::Typing;
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_BOOK_SEARCH), initial,
                                                           QUERY_LETTERS, InputType::Text);
  if (!keyboard) {
    LOG_ERR("BKS", "OOM: keyboard");
    leave();
    return;
  }
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) { onTyped(result); });
}

void BookSearchActivity::onTyped(const ActivityResult& result) {
  const auto* typed = result.isCancelled ? nullptr : std::get_if<KeyboardResult>(&result.data);
  const std::string query = typed ? trimmed(typed->text) : std::string();
  if (query.empty()) {
    // Nothing typed: back to the last results, or out when there are none.
    if (memory.hasResults()) showResults();
    else leave();
    return;
  }
  beginSearch(query);
}

void BookSearchActivity::beginSearch(const std::string& query) {
  const auto started = memory.runner.begin(query.c_str());
  memory.query = query;
  memory.nextSpine = 0;
  memory.chapters = epub ? epub->getSpineItemsCount() : 0;
  memory.incomplete = false;
  memory.failed = started != BookSearchRunner::Status::Ready;
  skipped = 0;
  startedAt = lastPaint = millis();
  LOG_INF("BKS", "begin query=%s chapters=%d status=%d", query.c_str(), memory.chapters, static_cast<int>(started));
  setTitle();
  if (memory.failed || memory.chapters <= 0) {
    memory.failed = true;
    showResults();
    return;
  }
  state = State::Searching;
  running = std::string();
  activeNav().reset();
  tenorchrome::noteFootActions(tr(STR_BOOK_SEARCH_STOP));
  requestUpdate();
}

void BookSearchActivity::showResults() {
  state = State::Results;
  setTitle();
  char full[64];
  snprintf(full, sizeof(full), tr(STR_BOOK_SEARCH_FULL), static_cast<int>(booksearch::kMaxResults));
  fullNote = full;
  activeNav().reset();
  tenorchrome::noteFootActions(nullptr);
  requestUpdate();
}

void BookSearchActivity::leave() {
  if (leaving) return;
  leaving = true;
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

void BookSearchActivity::endSearch(const bool stopped) {
  if (stopped) memory.incomplete = true;
  if (skipped) memory.incomplete = true;
  const auto* results = memory.runner.results();
  LOG_INF("BKS", "done results=%u chapters=%d next=%d skipped=%d bytes=%u ms=%lu stopped=%d failed=%d",
          static_cast<unsigned>(results ? results->count : 0), memory.chapters, memory.nextSpine, skipped,
          static_cast<unsigned>(memory.runner.bytesScanned()), static_cast<unsigned long>(millis() - startedAt),
          stopped ? 1 : 0, memory.failed ? 1 : 0);
  showResults();
}

void BookSearchActivity::stepSearch() {
  if (state != State::Searching || leaving) return;
  if (memory.nextSpine >= memory.chapters) {
    endSearch(false);
    return;
  }
  BookSearchRunner::Status status;
  {
    RenderLock lock(*this);
    status = memory.runner.scanChapter(*epub, memory.nextSpine);
  }
  ++memory.nextSpine;
  if (status == BookSearchRunner::Status::Full) {
    endSearch(false);
    return;
  }
  if (status == BookSearchRunner::Status::NoMemory) {
    // The scan cannot go on; what it found stays.
    memory.failed = true;
    endSearch(true);
    return;
  }
  if (status != BookSearchRunner::Status::Complete) ++skipped;  // a chapter that cannot be read: the rest goes on
  if (memory.nextSpine >= memory.chapters) {
    endSearch(false);
    return;
  }
  if (millis() - lastPaint >= PAINT_EVERY_MS) {
    lastPaint = millis();
    requestUpdate();
  }
}

void BookSearchActivity::loop() {
  UiListActivity::loop();
  stepSearch();
}

bool BookSearchActivity::handleCustomInput() {
  int x = 0, y = 0;
  if (!mappedInput.wasScreenTapped(x, y)) return false;
  if (state == State::Searching && tenorchrome::footActionAt(x, y) == 0) {
    endSearch(true);
    return true;
  }
  if (state == State::Results && HeaderBackTapTarget::nameContains(x, y)) {
    openKeyboard(memory.query);
    return true;
  }
  return false;
}

bool BookSearchActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (state == State::Searching) memory.incomplete = true;  // Back stops the scan; the hits so far stay
    leave();
    return true;
  }
  if (state == State::Results && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateIndex(nav.selected);
    return true;
  }
  return false;
}

void BookSearchActivity::activateIndex(const int index) {
  const auto* results = memory.runner.results();
  if (state != State::Results || !results || index < 0 || index >= static_cast<int>(results->count)) return;
  app.clearTapFlash();
  const auto& hit = results->items[index];
  ProgressChangeResult jump{};
  jump.spineIndex = hit.spine;
  jump.hasVisibleTextOffset = true;
  jump.visibleTextOffset = hit.visibleOffset;
  LOG_INF("BKS", "pick index=%d spine=%d offset=%u", index, hit.spine, static_cast<unsigned>(hit.visibleOffset));
  setResult(std::move(jump));
  finish();
}

const std::string& BookSearchActivity::chapterName(const int spine) const {
  if (spine != namedSpine) {
    const int toc = epub ? epub->getTocIndexForSpineIndex(spine) : -1;
    named = toc >= 0 ? epub->getTocItem(toc).title : std::string(tr(STR_UNNAMED));
    namedSpine = spine;
  }
  return named;
}

// The list lays out empty rows; drawPinMarks writes what is in them (the plain skin, as every touch screen).
void BookSearchActivity::provideRow(void*, const uint16_t index, fui::ListItem& item) {
  item.actionValue = static_cast<int16_t>(index);
}

void BookSearchActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  if (state == State::Searching) {
    char text[64];
    snprintf(text, sizeof(text), tr(STR_BOOK_SEARCH_RUNNING), std::min(memory.nextSpine + 1, memory.chapters),
             memory.chapters);
    running = text;
    screen.centeredText(running.c_str(), screen.theme().bodyText);
    return;
  }
  if (listCount() == 0) {
    screen.centeredText(memory.failed ? tr(STR_LIBRARY_SEARCH_UNAVAILABLE)
                                      : memory.incomplete ? tr(STR_SEARCH_INCOMPLETE) : tr(STR_BOOK_SEARCH_NONE),
                        screen.theme().bodyText);
    return;
  }
  fui::ListProps props;
  props.rowProvider = &BookSearchActivity::provideRow;
  props.rowProviderCtx = this;
  props.count = static_cast<uint16_t>(listCount());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.labelText = uiMenuLabelText(screen.theme());
  props.rowHeight = ROW_HEIGHT;
  syncListViewport(screen, props);
  screen.list(props);
}

// The plain skin's rows, in the place the list gave each: the chapter's name small, then the excerpt with the match in
// bold. The row after the hits is the list's end note, in the same small face.
void BookSearchActivity::drawPinMarks() {
  if (state != State::Results) return;
  const auto* results = memory.runner.results();
  if (!results) return;
  const int nameHeight = renderer.getLineHeight(NAME_FONT), textHeight = renderer.getLineHeight(TEXT_FONT);
  const int last = std::min(listCount(), activeNav().top + activeNav().pageRowsFor(listCount()) + 1);
  for (int row = activeNav().top; row < last; ++row) {
    const auto rect = app.publishedRect(ACTION_ROW, static_cast<int16_t>(row));
    if (rect.height <= 0) continue;
    const int x = rect.x + ROW_SIDE, room = rect.width - 2 * ROW_SIDE;
    if (row >= static_cast<int>(results->count)) {
      const char* note = endNote();
      if (note) renderer.drawText(NAME_FONT, x, rect.y + (rect.height - nameHeight) / 2, renderer.truncatedText(NAME_FONT, note, room).c_str());
      continue;
    }
    const auto& hit = results->items[row];
    const int top = rect.y + (rect.height - nameHeight - ROW_LINE_GAP - textHeight) / 2;
    renderer.drawText(NAME_FONT, x, top, renderer.truncatedText(NAME_FONT, chapterName(hit.spine).c_str(), room).c_str());
    const auto pieces = booksearchlayout::clipExcerpt(
        hit.excerpt, hit.matchStart, hit.matchLength, hit.excerptTruncated, room, renderer.getSpaceWidth(TEXT_FONT),
        [this](const std::string& text, const bool bold) {
          return renderer.getTextWidth(TEXT_FONT, text.c_str(), bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
        });
    const int y = top + nameHeight + ROW_LINE_GAP;
    int cursor = x;
    for (size_t i = 0; i < pieces.size(); ++i) {
      const auto style = pieces[i].bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
      renderer.drawText(TEXT_FONT, cursor, y, pieces[i].text.c_str(), true, style);
      cursor += renderer.getTextWidth(TEXT_FONT, pieces[i].text.c_str(), style);
      if (pieces[i].gapAfter) cursor += renderer.getSpaceWidth(TEXT_FONT);
    }
  }
  // The rows' ink goes under the list's faded ends too: drawRowFrame thinned its own lines there before this text
  // stood on them. The dither is by position, so a second pass over the same band takes away what the first took.
  constexpr int FADE_TOP_BAND = 64;  // UiListActivity::drawRowFrame's TOP_BAND
  if (fadeTopTo > fadeTopFrom) tenorchrome::fadeBand(renderer, fadeTopFrom, FADE_TOP_BAND, true);
  if (fadeFootTo > fadeFootFrom) tenorchrome::fadeBand(renderer, fadeFootFrom, fadeFootTo - fadeFootFrom, false);
}
#endif
