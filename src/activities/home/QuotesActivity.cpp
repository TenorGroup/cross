#include "QuotesActivity.h"

#include <I18n.h>
#include <Memory.h>

#include "activities/reader/DictionaryDefinitionActivity.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"

const char* QuotesActivity::headerTitle() const { return tr(STR_QUOTES); }
void QuotesActivity::onEnter() {
  RenderLock lock(*this);
  loadPage("", false);
  UiListActivity::onEnter();
}
void QuotesActivity::loadPage(const std::string& boundary, const bool previous) {
  quotes::list(boundary, previous, names);
  if (previous) {
    hasPrevious = names.size() > quotes::PAGE_SIZE;
    hasNext = true;
    if (hasPrevious) names.erase(names.begin());
  } else {
    hasNext = names.size() > quotes::PAGE_SIZE;
    hasPrevious = !boundary.empty();
    if (hasNext) names.pop_back();
  }
  if (names.empty()) hasPrevious = hasNext = false;
  count = 0;
  if (hasPrevious) labels[count++] = tr(STR_QUOTES_PREVIOUS);
  for (const auto& name : names) {
    QuoteRecord q;
    if (quotes::load(name, q)) {
      size_t length = std::min<size_t>(120, q.text.size());
      while (length < q.text.size() && length && (static_cast<unsigned char>(q.text[length]) & 0xc0) == 0x80) --length;
      labels[count] = q.text.substr(0, length);
    } else
      labels[count] = tr(STR_QUOTES_UNREADABLE);
    ++count;
  }
  if (hasNext) labels[count++] = tr(STR_QUOTES_NEXT);
  if (!count) labels[count++] = tr(STR_QUOTES_EMPTY);
  for (int i = 0; i < count; ++i) {
    rows[i].label = labels[i].c_str();
    rows[i].actionValue = i;
  }
  nav.reset();
}
void QuotesActivity::activateIndex(const int index) {
  if (hasPrevious && index == 0) {
    const auto key = names.front();
    {
      RenderLock lock(*this);
      loadPage(key, true);
    }
    requestUpdate();
    return;
  }
  if (hasNext && index == count - 1) {
    const auto key = names.back();
    {
      RenderLock lock(*this);
      loadPage(key, false);
    }
    requestUpdate();
    return;
  }
  const int item = index - (hasPrevious ? 1 : 0);
  if (item < 0 || item >= static_cast<int>(names.size())) return;
  QuoteRecord q;
  if (!quotes::load(names[item], q)) return;
  std::string body = q.title + "\n\n" + q.text;
  startActivityForResult(makeUniqueNoThrow<DictionaryDefinitionActivity>(
                             renderer, mappedInput, std::string(tr(STR_QUOTES)), std::move(body), false),
                         nullptr);
}
void QuotesActivity::buildScreen(UiScreen& screen) {
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
