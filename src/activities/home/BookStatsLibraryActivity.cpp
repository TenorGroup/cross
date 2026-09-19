#include "BookStatsLibraryActivity.h"

#include <I18n.h>
#include <Memory.h>

#include "BookStatsActivity.h"
#include "components/TenorMenuChrome.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
namespace {
bool fitsPageCache(const std::vector<ReadingStatsStore::BookEntry>& books) {
  size_t retained = books.capacity() * sizeof(ReadingStatsStore::BookEntry);
  for (const auto& book : books) retained += book.path.capacity() + book.title.capacity() + 2;
  return retained <= 4096;
}
}  // namespace
const char* BookStatsLibraryActivity::headerTitle() const { return tr(STR_STATS_BY_BOOK); }
void BookStatsLibraryActivity::onEnter() {
  RenderLock lock(*this);
  loadPage({}, false);
  UiListActivity::onEnter();
}
void BookStatsLibraryActivity::loadPage(const ReadingStatsStore::BookEntry& boundary, bool back) {
  const auto generation = READING_STATS.listGeneration();
  if (adjacentValid && generation == pageGeneration && !boundary.path.empty() && back == adjacentBefore) {
    books.swap(adjacentBooks);
    std::swap(previous, adjacentPrevious);
    std::swap(next, adjacentNext);
    adjacentBefore = !adjacentBefore;
    if (!fitsPageCache(adjacentBooks)) {
      adjacentValid = false;
      std::vector<ReadingStatsStore::BookEntry>().swap(adjacentBooks);
    }
  } else {
    adjacentValid = generation == pageGeneration && !boundary.path.empty() && !books.empty() && fitsPageCache(books);
    if (adjacentValid) {
      adjacentBooks.swap(books);
      adjacentPrevious = previous;
      adjacentNext = next;
      adjacentBefore = !back;
    } else {
      std::vector<ReadingStatsStore::BookEntry>().swap(adjacentBooks);
    }
    READING_STATS.listBooks(boundary, back, books);
    if (back) {
      previous = books.size() > 20;
      next = !books.empty();
      if (previous) books.erase(books.begin());
    } else {
      previous = !boundary.path.empty() && !books.empty();
      next = books.size() > 20;
      if (next) books.pop_back();
    }
  }
  pageGeneration = generation;
  count = 0;
  rows = {};
  if (previous) {
    rows[count].label = tr(STR_QUOTES_PREVIOUS);
    rows[count].actionValue = count;
    ++count;
  }
  for (const auto& book : books) {
    rows[count].label = book.title.c_str();
    rows[count].actionValue = count;
    ++count;
  }
  if (next) {
    rows[count].label = tr(STR_QUOTES_NEXT);
    rows[count].actionValue = count;
    ++count;
  }
  if (!count) {
    rows[0].label = READING_STATS.statisticsReadable ? tr(STR_STATS_NOT_RECORDED) : tr(STR_STATS_READ_ERROR);
    count = 1;
  }
  nav.reset();
}
void BookStatsLibraryActivity::activateIndex(int index) {
  if (previous && index == 0) {
    auto boundary = books.front();
    {
      RenderLock lock(*this);
      loadPage(boundary, true);
    }
    requestUpdate();
    return;
  }
  if (next && index == count - 1) {
    auto boundary = books.back();
    {
      RenderLock lock(*this);
      loadPage(boundary, false);
    }
    requestUpdate();
    return;
  }
  const int i = index - (previous ? 1 : 0);
  if (i < 0 || i >= static_cast<int>(books.size())) return;
  startActivityForResult(makeUniqueNoThrow<BookStatsActivity>(renderer, mappedInput, books[i].path, books[i].title),
                         nullptr);
}
void BookStatsLibraryActivity::drawChrome() {
  if (tenorchrome::enabled())
    tenorchrome::drawHeader(renderer, headerTitle(), tr(STR_HOME_TAB_STATS));
  else
    UiListActivity::drawChrome();
}
void BookStatsLibraryActivity::buildScreen(UiScreen& screen) {
  const auto& m = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(
      {static_cast<int16_t>(m.topPadding + m.headerHeight), 0, static_cast<int16_t>(m.buttonHintsHeight), 0});
  freeink::ui::ListProps props;
  props.items = rows.data();
  props.count = count;
  props.action = ACTION_ROW;
  props.labelText = uiMenuLabelText(screen.theme());
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}
