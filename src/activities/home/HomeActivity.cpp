#include "HomeActivity.h"

#include <FontCacheManager.h>

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <Utf8.h>
#include <Xtc.h>

#include <algorithm>
#include <cstring>
#include <functional>
#include <vector>

#include "BookStatsActivity.h"
#include "BookStatsLibraryActivity.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "DocThuMuc.h"
#include "FileFavorites.h"
#include "MappedInputManager.h"
#include "MenuCustomization.h"
#include "MenuFavorites.h"
#include "QuoteStore.h"
#include "QuotesActivity.h"
#include "ReadingHabitsActivity.h"
#include "ReadingHistoryActivity.h"
#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "SdCardFontSystem.h"
#include "SettingsList.h"
#include "activities/reader/ReaderActivity.h"
#include "activities/settings/SettingsActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/ReadingStatsFormat.h"
#include "components/ReadingStatsView.h"
#include "components/HomeStatsNavigation.h"
#include "components/HomeExcerptStyle.h"
#include "components/SettledListRender.h"
#include "components/TenorMenuChrome.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "components/X3SleepCover.h"
#include "components/themes/TenorRadius.h"
#include "components/icons/homeTabIcons.h"
#include "components/icons/tenorHomeTabIcons.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
constexpr StrId TAB_NAMES[HomeActivity::TAB_COUNT] = {StrId::STR_HOME_TAB_RECENT, StrId::STR_HOME_TAB_FOLDER,
                                                      StrId::STR_HOME_TAB_STATS, StrId::STR_SETTINGS_TITLE,
                                                      StrId::STR_READER_TAB_FAVORITES};
// Newest quote id of each book when Home last showed it (see homeQuoteIndex). It lives in RAM
// across Home visits and is lost at power-off, after which the card simply picks at random.
struct SeenQuote {
  uint32_t book = 0;
  uint64_t newest = 0;
};
constexpr uint8_t SEEN_BOOKS = 5;  // as many as the Recent tab lists
SeenQuote seenQuotes[SEEN_BOOKS];
uint8_t seenNext = 0;
uint64_t& seenNewest(const uint32_t book) {
  for (auto& seen : seenQuotes)
    if (seen.newest != 0 && seen.book == book) return seen.newest;
  auto& seen = seenQuotes[seenNext++ % SEEN_BOOKS];
  seen = SeenQuote{book, 0};
  return seen.newest;
}
}  // namespace

HomeActivity::HomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                           const HomeMenuItem initialMenuItemValue, const bool cleanInitialRefresh)
    : UiTabListActivity("Home", renderer, mappedInput),
      initialMenuItem(initialMenuItemValue),
      cleanInitialRefresh(cleanInitialRefresh) {}

freeink::ui::BitmapRef HomeActivity::tabIcon(const int index) const {
  // freeink::Icon dung the Mask1: bit 1 la de trong, bit 0 la ve muc. Doi sang BitmapRef
  // ngay tai cho, vi day la noi duy nhat can phep doi nay.
  static const freeink::Icon* const ANH[TAB_COUNT] = {&icon_tenor_home_recent_32, &icon_tenor_home_folder_32,
                                                      &icon_tenor_home_stats_32, &icon_tenor_home_settings_32,
                                                      &icon_tenor_home_favorites_32};
  static const freeink::Icon* const original[TAB_COUNT] = {&icon_home_recent_24, &icon_home_folder_24,
                                                           &icon_home_stats_24, &icon_home_settings_24,
                                                           &icon_tenor_home_favorites_32};
  freeink::ui::BitmapRef b;
  if (index < 0 || index >= TAB_COUNT) return b;
  const auto* icon = SETTINGS.uiTheme == CrossPointSettings::TENOR_UI ? ANH[index] : original[index];
  b.data = icon->bits;
  b.width = icon->w;
  b.height = icon->h;
  b.format = freeink::ui::BitmapFormat::Mask1;
  b.progmem = false;
  return b;
}

const char* HomeActivity::tabLabel(const int index) const { return I18N.get(TAB_NAMES[index]); }

void HomeActivity::onEnter() {
  menucustom::load();
  loadRecentBooks();
  READING_STATS.prepareHabits();

  // Coming back from a screen that lives under one tab lands on that tab.
  switch (initialMenuItem) {
    case HomeMenuItem::FILE_BROWSER:
    case HomeMenuItem::OPDS_BROWSER:
      activeTabId = Tab::FOLDER;
      break;
    case HomeMenuItem::FAVORITES_TAB:
      activeTabId = Tab::FAVORITES;
      break;
    case HomeMenuItem::STATS_TAB:
      activeTabId = Tab::STATS;
      break;
    case HomeMenuItem::FILE_TRANSFER:
    case HomeMenuItem::SETTINGS_MENU:
      activeTabId = Tab::CAI_DAT;
      break;
    default:
      activeTabId = Tab::RECENT;
      break;
  }
  rebuildRows();

  UiTabListActivity::onEnter();  // sizes the per-tab state; needs listCount()
  if (initialMenuItem == HomeMenuItem::RECENT_CONTINUE) {
    activeNav().selected = recentBooks.empty() ? 0 : 1;
    activeNav().top = 0;
    activeNav().followOnBuild = true;
  }
  requestUpdate();
}

void HomeActivity::restoreNavigation(const MenuNavigationState& state) {
  MenuNavigationState restored = state;
  if (initialMenuItem != HomeMenuItem::NONE) restored.tab = activeTab();
  UiTabListActivity::restoreNavigation(restored);
  // Returning from a book focuses Continue; other tabs retain their cursor memory.
  if (initialMenuItem == HomeMenuItem::RECENT_CONTINUE) {
    RenderLock lock(*this);
    activeNav().selected = recentBooks.empty() ? 0 : 1;
    activeNav().top = 0;
    activeNav().followOnBuild = true;
    return;
  }
  if (restored.tab != state.tab || state.selection.empty()) return;
  RenderLock lock(*this);
  if (activeTabId == Tab::FAVORITES) {
    const auto found = std::find(favoriteKeys.begin(), favoriteKeys.end(), state.selection);
    if (found != favoriteKeys.end()) activeNav().selected = static_cast<int>(found - favoriteKeys.begin()) + 1;
  } else if (activeTabId == Tab::RECENT) {
    const auto found = std::find_if(recentBooks.begin(), recentBooks.end(),
                                    [&](const RecentBook& book) { return book.path == state.selection; });
    activeNav().selected =
        found == recentBooks.end() ? (recentBooks.empty() ? 0 : 1) : static_cast<int>(found - recentBooks.begin()) + 1;
  } else if (activeTabId == Tab::CAI_DAT) {
    for (size_t i = 0; i < settingsGroups.size(); ++i)
      if (state.selection == "group/" + std::to_string(settingsGroups[i])) activeNav().selected = i + 2;
  }
  activeNav().followOnBuild = true;
}

void HomeActivity::captureNavigation(MenuNavigationState& state) const {
  UiTabListActivity::captureNavigation(state);
  state.selection.clear();
  const int row = ringPos() - 1;
  if (activeTabId == Tab::FAVORITES) state.selection = favoriteKey(row);
  if (activeTabId == Tab::RECENT && row >= 0 && row < static_cast<int>(recentBooks.size()))
    state.selection = recentBooks[row].path;
  if (activeTabId == Tab::CAI_DAT && row > 0 && row <= static_cast<int>(settingsGroups.size()))
    state.selection = "group/" + std::to_string(settingsGroups[row - 1]);
}

void HomeActivity::onExit() {
  freeCoverBuffer();
  UiTabListActivity::onExit();
}

void HomeActivity::onPause() {
  // The manager holds RenderLock while pausing an activity.
  freeCoverBuffer();
  coverRendered = false;
}

void HomeActivity::selectTab(const Tab tab) {
  // The render task reads activeTabId and the row arrays while it builds the
  // screen, and a tab step arrives from the unlocked button path.
#ifdef TENOR_UI_ACCEPTANCE
  const uint32_t selectStartedUs = micros();
  uint32_t rebuildRowsUs = 0;
#endif
  {
    RenderLock lock(*this);
    activeTabId = tab;
    favoriteFileMissing = false;
    // The tenor card stays fixed while browsing Home tabs. Reuse its existing
    // bitmap on wrap; onPause/onExit release it before opening another activity.
    if (tab != Tab::RECENT && !tenorchrome::enabled()) {
      freeCoverBuffer();
      coverRendered = false;
    }
#ifdef TENOR_UI_ACCEPTANCE
    const uint32_t rebuildStartedUs = micros();
#endif
    rebuildRows();
#ifdef TENOR_UI_ACCEPTANCE
    rebuildRowsUs = micros() - rebuildStartedUs;
#endif
    if (!mappedInput.hasTouch() && listCount() > 0 && activeNav().selected <= 0) activeNav().selected = 1;
    activeNav().followOnBuild = true;
  }
#ifdef TENOR_UI_ACCEPTANCE
  LOG_INF("HOME_PROBE", "tab_select_us=%lu rebuild_rows_us=%lu tab=%d",
          static_cast<unsigned long>(micros() - selectStartedUs), static_cast<unsigned long>(rebuildRowsUs),
          static_cast<int>(tab));
#endif
}

void HomeActivity::stepTab(const int direction) {
  const int next = adjacentTab(direction);
  selectTab(static_cast<Tab>(next));
  requestUpdate();
}

void HomeActivity::onTabAction(const int index) {
  selectTab(static_cast<Tab>(index));
  app.clearTapFlash();
}

// Rows of the active tab. Structural: call it when the tab changes or the
// recent list reloads, never from buildScreen().
void HomeActivity::rebuildRows() {
  rowItems.clear();
  rowLabels.clear();
  favoriteKeys.clear();
  favoriteValues.clear();
  settingsGroups.clear();

  switch (activeTabId) {
    case Tab::RECENT:
      for (const auto& book : recentBooks) rowLabels.push_back(book.title);
      break;
    case Tab::FOLDER:
      docGocTheNho();
      for (const auto& muc : mucTheNho) rowLabels.push_back(muc);
      break;
    case Tab::STATS: {
      const bool enlarged = normalizedUiTextSize(SETTINGS.uiTextSize) != 0;
      if (enlarged != statsRowsEnlarged && activeNav().selected > 0) {
        activeNav().selected = statsSelectionForTier(activeNav().selected, statsRowsEnlarged, enlarged);
        activeNav().followOnBuild = true;
      }
      statsRowsEnlarged = enlarged;
      if (enlarged) rowLabels.emplace_back(statsPage ? tr(STR_PREV_PAGE) : tr(STR_NEXT_PAGE));
      rowLabels.emplace_back(tr(STR_READING_HABITS));
      rowLabels.emplace_back(tr(STR_STATS_BY_BOOK));
      rowLabels.emplace_back(tr(STR_STATS_MONTH));
      rowLabels.emplace_back(tr(STR_QUOTES));
      rowLabels.emplace_back(tr(STR_STATS_RESET_ALL));
      rowLabels.emplace_back(tr(STR_STATS_RESET_HABITS));
      break;
    }
    case Tab::FAVORITES:
      break;
    case Tab::CAI_DAT: {
      rowLabels.emplace_back(tr(STR_FILE_TRANSFER));
      for (int i = 0; i < settingstabs::TAB_COUNT; ++i) {
        const int id = menucustom::idAt(1, i, settingstabs::TAB_COUNT);
        settingsGroups.push_back(id);
        rowLabels.emplace_back(I18N.get(settingstabs::tenThe(static_cast<settingstabs::Tab>(id))));
      }
      break;
    }
  }

  if (activeTabId == Tab::FAVORITES && menucustom::state().pinCount > 0) {
    for (int i = 0; i < menucustom::state().pinCount; ++i) {
      const std::string key = menucustom::state().pins[i].data();
      if (filefavorites::isFileKey(key)) {
        const auto path = filefavorites::pathFor(key);
        favoriteKeys.push_back(key);
        favoriteValues.emplace_back(key.rfind("folder/", 0) == 0 ? tr(STR_HOME_TAB_FOLDER) : "");
        rowLabels.push_back(path.empty() ? tr(STR_DICT_NOT_FOUND)
                                         : utf8ComposeNfc(path.substr(path.find_last_of('/') + 1)));
        continue;
      }
      const auto name = menufavorites::label(key);
      if (name == StrId::STR_NONE_OPT) continue;
      favoriteKeys.push_back(key);
      favoriteValues.push_back(menufavorites::value(key, &sdFontSystem.registry()));
      rowLabels.emplace_back(I18N.get(name));
    }
  }
  rowItems.reserve(rowLabels.size());
  for (size_t i = 0; i < rowLabels.size(); i++) {
    fui::ListItem item;
    item.label = rowLabels[i].c_str();
    if (activeTabId == Tab::FAVORITES) item.value = favoriteValues[i].c_str();
    item.actionValue = static_cast<int16_t>(i);
    rowItems.push_back(item);
  }
}

void HomeActivity::activateIndex(const int index) {
  commitTabNavigation();
  if (index < 0 || index >= static_cast<int>(rowLabels.size())) return;
  app.clearTapFlash();

  switch (activeTabId) {
    case Tab::RECENT:
      onSelectBook(recentBooks[index].path);
      return;
    case Tab::FOLDER: {
      const std::string& ten = mucTheNho[static_cast<size_t>(index)];
      if (!ten.empty() && ten.back() == '/') {
        activityManager.goToFileBrowser("/" + ten.substr(0, ten.size() - 1));
      } else {
        onSelectBook("/" + ten);
      }
      return;
    }
    case Tab::STATS: {
      if (statsRowsEnlarged && index == 0) {
        RenderLock lock(*this);
        statsPage = 1 - statsPage;
        rebuildRows();
        activeNav().followOnBuild = true;
        requestUpdate();
        return;
      }
      const int action = statsActionIndex(index, statsRowsEnlarged);
      if (action == 0)
        startActivityForResult(makeUniqueNoThrow<ReadingHabitsActivity>(renderer, mappedInput), nullptr);
      else if (action == 1)
        startActivityForResult(makeUniqueNoThrow<BookStatsLibraryActivity>(renderer, mappedInput), nullptr);
      else if (action == 2)
        startActivityForResult(makeUniqueNoThrow<ReadingHistoryActivity>(renderer, mappedInput), nullptr);
      else if (action == 3)
        startActivityForResult(makeUniqueNoThrow<QuotesActivity>(renderer, mappedInput),
                               [this](const ActivityResult& result) {
                                 // A quote deleted or trimmed there may be the one a card shows,
                                 // so every card picks its quote again and is drawn anew.
                                 if (result.isCancelled) return;
                                 RenderLock lock(*this);
                                 cardQuotesPicked = 0;
                                 std::fill(std::begin(cardQuotes), std::end(cardQuotes), 0);
                                 freeCoverBuffer();
                               });
      else
        confirmStatsReset(action == 4);
      return;
    }
    case Tab::CAI_DAT:
      if (index == 0) {
        activityManager.goToFileTransfer();
        return;
      }
      freeCoverBuffer();
      {
        const int group = settingsGroups[index - 1];
        startActivityForResult(makeUniqueNoThrow<SettingsActivity>(renderer, mappedInput, group, true),
                               [this, group](const ActivityResult&) {
                                 RenderLock lock(*this);
                                 rebuildRows();
                                 const auto found = std::find(settingsGroups.begin(), settingsGroups.end(), group);
                                 activeNav().selected = static_cast<int>(found - settingsGroups.begin()) + 2;
                                 activeNav().followOnBuild = true;
                               });
      }
      return;
    case Tab::FAVORITES: {
      const std::string key = favoriteKeys[index];
      freeCoverBuffer();
      if (key == "action/15") {
        activityManager.goToFileTransfer();
        return;
      }
      if (filefavorites::isFileKey(key)) {
        const auto path = filefavorites::pathFor(key);
        favoriteFileMissing = path.empty() || !Storage.exists(path.c_str());
        if (favoriteFileMissing) {
          requestUpdate();
          return;
        }
        if (key.rfind("folder/", 0) == 0)
          activityManager.goToFileBrowser(path);
        else
          onSelectBook(path);
        return;
      }
      auto target = menufavorites::open(key, renderer, mappedInput);
      if (target)
        startActivityForResult(std::move(target), [this, key](const ActivityResult&) {
          RenderLock lock(*this);
          rebuildRows();
          const auto found = std::find(favoriteKeys.begin(), favoriteKeys.end(), key);
          activeNav().selected =
              found == favoriteKeys.end() ? (listCount() ? 1 : 0) : static_cast<int>(found - favoriteKeys.begin()) + 1;
          activeNav().followOnBuild = true;
        });
      return;
    }
  }
}

bool HomeActivity::handleButtons() {
  // Holding the front previous button brings the card back to the most recent book.
  // Queued like every other move: the loop must not wait for the panel.
  if (activeTabId == Tab::RECENT && tenorchrome::enabled() &&
      mappedInput.wasLongPressed(MappedInputManager::Button::Left, 700)) {
    queueNavIntent(NavIntent::FirstRow);
    return true;
  }
  if (mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, 700)) {
    const int index = ringPos() - 1;
    // Giu Chon tren THANH THE Folder: mo trinh duyet tep tai goc the nho. Nhip giu nay truoc
    // 14/09/2026 dem bo khong, con dong "Mo folder" thi trung voi chinh the (T9).
    if (activeTabId == Tab::FOLDER && index < 0) {
      activityManager.goToFileBrowser("/");
      return true;
    }
    std::string path;
    if (activeTabId == Tab::RECENT && index >= 0 && index < static_cast<int>(recentBooks.size()))
      path = recentBooks[index].path;
    if (activeTabId == Tab::FOLDER && index >= 0 && index < static_cast<int>(mucTheNho.size()) &&
        mucTheNho[index].back() != '/')
      path = "/" + mucTheNho[index];
    if (!path.empty()) {
      {
        // Free the card before the reader allocates. onPause() releases it too,
        // so a panel that is mid-refresh is left to do it rather than waited on.
        RenderLock coverLock(RenderLock::TryTake{});
        if (coverLock.acquired()) {
          freeCoverBuffer();
          coverRendered = false;
        }
      }
      startActivityForResult(ReaderActivity::create(renderer, mappedInput, path, false, true), nullptr);
    }
    return true;
  }
  // Back opens the most recently read book: it is otherwise unused here, and
  // recentBooks is most-recent-first and already pruned of missing files.
  if (backReleased()) {
    if (!recentBooks.empty()) onSelectBook(recentBooks[0].path);
    return true;
  }

  if (confirmReleased()) {
    if (ringPos() == 0) {
      // Step into the tab's first row; the edge buttons change tab.
      queueNavIntent(NavIntent::FirstRow);
    } else {
      activateIndex(ringPos() - 1);
    }
    return true;
  }

  return false;
}

void HomeActivity::confirmStatsReset(const bool all) {
  statsResetTip.reset();
  startActivityForResult(
      makeUniqueNoThrow<ConfirmationActivity>(
          renderer, mappedInput, I18N.get(all ? StrId::STR_STATS_RESET_ALL : StrId::STR_STATS_RESET_HABITS),
          I18N.get(all ? StrId::STR_STATS_RESET_ALL_BODY : StrId::STR_STATS_RESET_HABITS_BODY)),
      [this, all](const ActivityResult& result) {
        if (result.isCancelled) return;
        RenderLock lock(*this);
        switch (READING_STATS.resetStatistics(all)) {
          case ReadingStatsStore::ResetResult::Failed:
            statsResetTip = StrId::STR_STATS_RESET_FAILED;
            break;
          case ReadingStatsStore::ResetResult::Pending:
            statsResetTip = StrId::STR_STATS_RESET_PENDING;
            break;
          case ReadingStatsStore::ResetResult::Complete:
            break;
        }
        rebuildRows();
        requestUpdate();
      });
}

const char* HomeActivity::habitSuggestion() const {
  if (!READING_STATS.statisticsReadable) return nullptr;
  const auto& ledger = READING_STATS.habitLedger;
  const auto stamp = ReadingStatsStore::habitStamp();
  const uint8_t visible = stamp.day && !ledger.clockLost ? ledger.awarded & ~ledger.hidden : 0;
  constexpr StrId names[] = {StrId::STR_HABIT_NIGHT,   StrId::STR_HABIT_SHORT,   StrId::STR_HABIT_EARLY,
                             StrId::STR_HABIT_REGULAR, StrId::STR_HABIT_WEEKEND, StrId::STR_HABIT_LONG};
  for (size_t i = 0; i < habits::NAMES; ++i)
    if (visible & (1 << i)) return I18N.get(names[i]);
  return nullptr;
}

void HomeActivity::drawFooter() {
  // Back opens the most recent book from every tab (handleButtons). Its hint is the return symbol
  // every screen draws for Back, hidden when there is no book to open. On the tenor Recent tab the
  // front buttons step between books and Select opens the one shown; hints for a button that would
  // do nothing are left out.
  const bool hasRecent = !recentBooks.empty();
  const char* back = hasRecent ? tr(STR_BACK) : "";
  const bool card = activeTabId == Tab::RECENT && tenorchrome::enabled();
  const bool several = recentBooks.size() > 1;
  const auto labels =
      card ? mappedInput.mapLabels(back, hasRecent ? tr(STR_SELECT) : "", several ? tr(STR_DIR_LEFT) : "",
                                   several ? tr(STR_DIR_RIGHT) : "")
           : mappedInput.mapLabels(back, tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  if (activeTabId == Tab::FOLDER && ringPos() == 0) tenorchrome::drawTip(renderer, tr(STR_FOLDER_HOLD));
  if (favoriteFileMissing) tenorchrome::drawTip(renderer, tr(STR_FILE_NOT_FOUND), 1, 3);
  if (activeTabId == Tab::STATS && statsResetTip) tenorchrome::drawTip(renderer, I18N.get(*statsResetTip), 1, 2);
}

void HomeActivity::drawChrome() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();

  // Band spans topPadding..homeTopPadding: the cover tile starts at the fixed
  // homeTopPadding, so the height must shrink by topPadding or the band sinks
  // into the tile.
  // Dong tieu de mang TEN THE dang mo. Thanh the o man chinh chi co bieu tuong, nen day
  // la cho duy nhat nguoi moi cam may hoc duoc bon cai ten, va no ton 0 nhip bam: ten tu
  // doi khi nhay the. Truoc 14/09 cho nay de ten cuon sach gan nhat, thu von da hien lai
  // ngay duoi trong o anh bia va trong danh sach.
  if (tenorchrome::enabled())
    drawNavigationHeader(tabLabel(activeTab()));
  else
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding - metrics.topPadding},
                   tabLabel(activeTab()));

  if (activeTabId == Tab::STATS) {
    const auto* suggestion = habitSuggestion();
    const int top = coverTileTop() + 12;
    if (suggestion) {
      renderer.drawText(UI_12_FONT_ID, 24, top,
                        renderer.truncatedText(UI_12_FONT_ID, suggestion, pageWidth - 48, EpdFontFamily::BOLD).c_str(),
                        true, EpdFontFamily::BOLD);
    }
    const bool enlarged = normalizedUiTextSize(SETTINGS.uiTextSize) != 0;
    const int badge = enlarged ? renderer.getLineHeight(UI_12_FONT_ID) + 8 : readingstatsview::BADGE_HEIGHT;
    readingstatsview::draw(renderer, top + (suggestion ? badge : 0), false, suggestion, enlarged ? statsPage : -1);
  }
  if (activeTabId != Tab::RECENT) return;
  if (tenorchrome::enabled()) {
    drawRecentCard();
    return;
  }

  // Record the tile rect so storeCoverBuffer knows which sub-region to snapshot.
  coverRectX = 0;
  coverRectY = coverTileTop();
  coverRectW = pageWidth;
  coverRectH = metrics.homeCoverTileHeight;
  textRectH = 0;
  bool bufferRestored = coverBufferStored && restoreCoverBuffer();
  GUI.drawRecentBookCover(renderer, Rect{0, coverTileTop(), pageWidth, metrics.homeCoverTileHeight}, recentBooks,
                          tenorchrome::enabled() ? -1 : std::max(0, ringPos() - 1), coverRendered, coverBufferStored,
                          bufferRestored, std::bind(&HomeActivity::storeCoverBuffer, this));
}

// Top of the cover tile: it sits under the header and the tab band, so both the
// chrome pass that draws it and the screen pass that reserves room for it have
// to agree on one number.
int HomeActivity::tabBarTop() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  return SETTINGS.uiTheme == CrossPointSettings::TENOR_UI && !mappedInput.hasTouch()
             ? tenorchrome::tabTop()
             : metrics.topPadding + metrics.headerHeight;
}

int HomeActivity::preferredTabBarHeight() const {
  if (SETTINGS.uiTheme == CrossPointSettings::TENOR_UI && !mappedInput.hasTouch()) {
    // Include 2 px above, 4 below and the divider: the filled box equals a row.
    return tenorchrome::tabHeight();
  }
  return UiTabListActivity::preferredTabBarHeight();
}

int HomeActivity::coverTileTop() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  if (SETTINGS.uiTheme != CrossPointSettings::TENOR_UI || mappedInput.hasTouch())
    return metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight;
  return tabBarTop() + preferredTabBarHeight() + 16;
}

void HomeActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(tabBarTop()), 0, static_cast<int16_t>(metrics.buttonHintsHeight), 0});

  buildTabBar(screen);
  if (activeTabId == Tab::RECENT && tenorchrome::enabled()) {
    // drawChrome() paints the whole card; no list rows are drawn. The ring still counts one row
    // per book, so the front buttons walk the books with the usual wrap, and the viewport spans
    // them all: no "more below" chevron, and holding a front button reaches the first or last.
    auto& n = activeNav();
    const int count = listCount();
    n.selected = count > 0 ? std::clamp(n.selected, 1, count) : 0;
    n.top = 0;
    n.visibleRows = std::max(1, count);
    n.drawnRows = count;
    n.drawnCount = count;
    n.followOnBuild = false;
    n.followPending = false;
    return;
  }
  // Leave the cover tile's band to drawChrome(); the list starts under it.
  if (activeTabId == Tab::RECENT) screen.takeTop(static_cast<int16_t>(metrics.homeCoverTileHeight));

  if (activeTabId == Tab::STATS) screen.takeTop(static_cast<int16_t>(statsPanelHeight() + 12));

  if (activeTabId == Tab::FAVORITES && rowItems.empty()) {
    tenorchrome::drawTip(renderer, tr(STR_HOME_FAVORITES_HINT), 0, 6);
    return;
  }
  if (activeTabId == Tab::FOLDER && rowItems.empty()) {
    screen.centeredText(tr(STR_NO_FILES_FOUND), screen.theme().bodyText);
    return;
  }

  if (favoriteFileMissing)
    screen.takeBottom(static_cast<int16_t>(28 + tenorchrome::tipHeight(renderer, tr(STR_FILE_NOT_FOUND), 3)));
  if (activeTabId == Tab::STATS && statsResetTip)
    screen.takeBottom(static_cast<int16_t>(28 + tenorchrome::tipHeight(renderer, I18N.get(*statsResetTip), 2)));
  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.labelText = uiMenuLabelText(screen.theme());
  props.labelText.maxLines = 2;
  syncTabListViewport(screen, props);
  screen.list(props);
}

void HomeActivity::render(RenderLock&&) {
  if (activeTabId == Tab::STATS && statsRowsEnlarged != (normalizedUiTextSize(SETTINGS.uiTextSize) != 0))
    rebuildRows();
#ifdef TENOR_UI_ACCEPTANCE
  const uint32_t totalStartedUs = micros();
  const uint32_t paintStartedUs = totalStartedUs;
#endif
  const uint32_t started = millis();
  renderer.clearScreen();
  drawChrome();
#ifdef TENOR_UI_ACCEPTANCE
  const uint32_t chromeUs = micros() - paintStartedUs;
  const uint32_t uiStartedUs = micros();
#endif
  unsigned paintPass = 0;
  renderSettledList(activeNav(), [&] {
    if (paintPass++ > 0) {
      renderer.clearScreen();
      drawChrome();
    }
    renderUi();
  });
#ifdef TENOR_UI_ACCEPTANCE
  const unsigned rebuildPasses = paintPass - 1;
#endif
#ifdef TENOR_UI_ACCEPTANCE
  const uint32_t uiUs = micros() - uiStartedUs;
  const uint32_t footerStartedUs = micros();
#endif
  drawFooter();
#ifdef TENOR_UI_ACCEPTANCE
  const uint32_t footerUs = micros() - footerStartedUs;
  const uint32_t paintUs = micros() - paintStartedUs;
  const uint32_t displayStartedUs = micros();
#endif
  renderer.displayBuffer(cleanInitialRefresh ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH);
#ifdef TENOR_UI_ACCEPTANCE
  const uint32_t displayUs = micros() - displayStartedUs;
  LOG_INF("HOME_PROBE", "frame_paint_us=%lu display_us=%lu total_us=%lu",
          static_cast<unsigned long>(paintUs), static_cast<unsigned long>(displayUs),
          static_cast<unsigned long>(micros() - totalStartedUs));
  LOG_INF("HOME_PROBE", "chrome_us=%lu ui_us=%lu footer_us=%lu rebuild_passes=%u",
          static_cast<unsigned long>(chromeUs), static_cast<unsigned long>(uiUs),
          static_cast<unsigned long>(footerUs), rebuildPasses);
#endif
  cleanInitialRefresh = false;
  LOG_INF("HOME", "Frame row=%d top=%d total=%lums heap=%u", ringPos(), activeNav().top,
          static_cast<unsigned long>(millis() - started), ESP.getFreeHeap());
}

bool HomeActivity::storeCoverBuffer() {
  // drawChrome() must have already set the cover rect; without it we'd be back
  // to cloning the whole framebuffer.
  if (coverRectW <= 0 || coverRectH <= 0) return false;
  freeCoverBuffer();
  const size_t first = renderer.getRegionByteSize(coverRectX, coverRectY, coverRectW, coverRectH);
  const size_t second =
      textRectW > 0 && textRectH > 0 ? renderer.getRegionByteSize(textRectX, textRectY, textRectW, textRectH) : 0;
  const size_t needed = first + second;
  if (first == 0) return false;
  coverBuffer = static_cast<uint8_t*>(malloc(needed));
  if (!coverBuffer) {
    LOG_ERR("HOME", "OOM: cover buffer (%u bytes)", (unsigned)needed);
    return false;
  }
  coverBufferSize = needed;
  coverBufferUiSize = normalizedUiTextSize(SETTINGS.uiTextSize);
  if (!renderer.copyRegionToBuffer(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer, first) ||
      (second > 0 &&
       !renderer.copyRegionToBuffer(textRectX, textRectY, textRectW, textRectH, coverBuffer + first, second))) {
    free(coverBuffer);
    coverBuffer = nullptr;
    coverBufferSize = 0;
    return false;
  }
  return true;
}

bool HomeActivity::restoreCoverBuffer() {
  if (coverBufferUiSize != normalizedUiTextSize(SETTINGS.uiTextSize)) {
    freeCoverBuffer();
    return false;
  }
  if (!coverBuffer || coverRectW <= 0 || coverRectH <= 0) return false;
  const size_t first = renderer.getRegionByteSize(coverRectX, coverRectY, coverRectW, coverRectH);
  if (first > coverBufferSize ||
      !renderer.copyBufferToRegion(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer, first))
    return false;
  return first == coverBufferSize || renderer.copyBufferToRegion(textRectX, textRectY, textRectW, textRectH,
                                                                 coverBuffer + first, coverBufferSize - first);
}

void HomeActivity::freeCoverBuffer() {
  if (coverBuffer) {
    free(coverBuffer);
    coverBuffer = nullptr;
  }
  coverBufferSize = 0;
  coverBufferStored = false;
  coverBufferBook = -1;
}

void HomeActivity::loadRecentBooks() {
  recentBooks.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  recentBooks.reserve(std::min(books.size(), RECENT_LIMIT));
  for (const RecentBook& book : books) {
    if (RecentBooksStore::isMissing(book)) continue;
    recentBooks.push_back(book);
    if (recentBooks.size() == RECENT_LIMIT) break;
  }
}

void HomeActivity::onSelectBook(const std::string& path) { activityManager.goToReader(path); }

void HomeActivity::docGocTheNho() {
  // Vung nho hung ten file muon roi tra ngay trong ham nay. Giu no song suot doi man
  // chinh la an them RAM ma khong lam cuon sach de doc hon.
  constexpr size_t DEM_CO = 500;  // bang FileBrowserActivity::NAME_BUFFER_SIZE
  auto dem = makeUniqueNoThrow<char[]>(DEM_CO);
  if (!dem) {
    mucTheNho.clear();
    return;
  }
  docthumuc::doc("/", SETTINGS.showHiddenFiles, docthumuc::Loc::Sach, dem.get(), DEM_CO, mucTheNho);
}

std::string HomeActivity::favoriteKey(int row) const {
  if (activeTabId == Tab::RECENT && row >= 0 && row < static_cast<int>(recentBooks.size()))
    return filefavorites::keyFor(recentBooks[row].path, false);
  if (activeTabId == Tab::FOLDER && row >= 0 && row < static_cast<int>(mucTheNho.size())) {
    auto path = "/" + mucTheNho[row];
    const bool folder = path.back() == '/';
    if (folder) path.pop_back();
    return filefavorites::keyFor(path, folder);
  }
  if (activeTabId == Tab::CAI_DAT && row == 0) return "action/15";
  if (activeTabId != Tab::FAVORITES || row < 0 || row >= static_cast<int>(favoriteKeys.size())) return {};
  return favoriteKeys[row];
}
void HomeActivity::favoritesChanged() {
  const int selected = activeNav().selected;
  rebuildRows();
  activeNav().selected = std::min(selected, listCount());
  if (listCount() && activeNav().selected <= 0) activeNav().selected = 1;
  activeNav().followOnBuild = true;
}
bool HomeActivity::giuNutDiDong(int direction) {
  if (activeTabId != Tab::FAVORITES) return false;
  const auto button = direction > 0 ? MappedInputManager::Button::Right : MappedInputManager::Button::Left;
  if (!mappedInput.wasLongPressed(button, 700)) return true;
  const std::string key = favoriteKey(ringPos() - 1);
  if (key.empty()) return true;
  RenderLock lock(*this);
  menucustom::movePin(key.c_str(), direction);
  rebuildRows();
  const auto found = std::find(favoriteKeys.begin(), favoriteKeys.end(), key);
  if (found != favoriteKeys.end()) activeNav().selected = static_cast<int>(found - favoriteKeys.begin()) + 1;
  activeNav().followOnBuild = true;
  requestUpdate();
  return true;
}

bool HomeActivity::toggleFavorite(int row) {
  favoriteFileMissing = false;
  if (activeTabId == Tab::RECENT && row >= 0 && row < static_cast<int>(recentBooks.size()))
    return filefavorites::toggle(recentBooks[row].path, false);
  if (activeTabId == Tab::FOLDER && row >= 0 && row < static_cast<int>(mucTheNho.size())) {
    auto path = "/" + mucTheNho[row];
    const bool folder = path.back() == '/';
    if (folder) path.pop_back();
    return filefavorites::toggle(path, folder);
  }
  const auto key = favoriteKey(row);
  if (filefavorites::isFileKey(key)) return filefavorites::unpin(key);
  return UiListActivity::toggleFavorite(row);
}

int HomeActivity::statsPanelHeight() const {
  if (normalizedUiTextSize(SETTINGS.uiTextSize) == 0) return readingstatsview::HEIGHT;
  return readingstatsview::panelHeight(renderer, statsPage) +
         (habitSuggestion() ? renderer.getLineHeight(UI_12_FONT_ID) + 8 : 0);
}

int HomeActivity::shownRecent() const {
  const int count = static_cast<int>(recentBooks.size());
  return count == 0 ? -1 : std::clamp(ringPos() - 1, 0, count - 1);
}

std::string HomeActivity::cardExcerpt(const int index, bool& quoted) {
  quoted = false;
  const auto& book = recentBooks[index];
  const uint8_t bit = static_cast<uint8_t>(1u << index);
  if (!(cardQuotesPicked & bit)) {
    // Picked once per visit, not per repaint. listNames() reads names only, so no other book's
    // record is opened; the one quote shown is the only record read below.
    cardQuotesPicked |= bit;
    std::vector<quotes::QuoteId> ids;
    const uint32_t key = quotes::bookKey(book.path);
    quotes::listNames(key, ids);
    if (!ids.empty()) {
      uint64_t& seen = seenNewest(key);
      // The quote kept or edited last, which the card has not shown yet; it survives deep sleep,
      // unlike `seen`. Spent once shown, so the next visit picks again.
      quotes::QuoteId marked = 0;
      if (!quotes::latestSaved(marked) || quotes::bookKeyOfName(marked) != key) marked = 0;
      cardQuotes[index] =
          ids[homeQuoteIndex(ids.data(), ids.size(), seen, static_cast<uint32_t>(random(0x7FFFFFFF)), marked)];
      if (marked != 0 && cardQuotes[index] == marked) quotes::forgetLatestSaved();
      seen = ids.front();
      LOG_INF("HOME", "Card quote %s of %u", quotes::nameOf(cardQuotes[index]).c_str(),
              static_cast<unsigned>(ids.size()));
    }
  }
  QuoteRecord quote;
  if (cardQuotes[index] != 0 && quotes::load(cardQuotes[index], quote)) {
    quoted = true;
    return quote.text;
  }
  return book.excerpt;
}

void HomeActivity::loadCardStats(const RecentBook& book) {
  BookReadingRecord record;
  cardStats = {};
  cardStats.recorded = READING_STATS.readBook(book.path, record);
  const uint64_t elapsed = static_cast<uint64_t>(record.minutes) * 60000 + record.remainderMs;
  cardStats.rows = homeStatRows(cardStats.recorded, elapsed, record.days, record.firstDay, record.lastDay);
  cardStats.percent = std::min<uint8_t>(record.progress, 100);
  auto& values = cardStats.values;
  char text[64], duration[48];
  if (!cardStats.recorded) {
    values[HOME_STAT_READ] = tr(STR_STATS_NOT_RECORDED);
    return;
  }
  snprintf(text, sizeof(text), "%u%%", static_cast<unsigned>(cardStats.percent));
  values[HOME_STAT_READ] = text;
  readingstatsview::duration(elapsed, text, sizeof(text));
  values[HOME_STAT_TOTAL] = text;
  if (record.days) {
    readingstatsview::duration(elapsed / record.days, duration, sizeof(duration));
    snprintf(text, sizeof(text), tr(STR_RECENT_STAT_PER_DAY), duration);
    values[HOME_STAT_AVERAGE] = text;
    snprintf(text, sizeof(text), tr(STR_RECENT_STAT_DAYS_VALUE), static_cast<unsigned>(record.days));
    values[HOME_STAT_DAYS] = text;
  }
  snprintf(text, sizeof(text), tr(STR_RECENT_STAT_SPAN_VALUE), static_cast<unsigned>(record.firstDay % 100),
           static_cast<unsigned>(record.firstDay / 100 % 100), static_cast<unsigned>(record.lastDay % 100),
           static_cast<unsigned>(record.lastDay / 100 % 100));
  values[HOME_STAT_SPAN] = text;
}

// Label in the small face, value in the title face (bold), stepping down one size when the value is
// wider than the column, as in the approved drawing. Returns the top of the progress bar, -1 when
// none is drawn.
int HomeActivity::drawCardStats(const HomeCardLayout& card) {
  static constexpr StrId LABELS[HOME_STAT_COUNT] = {StrId::STR_RECENT_STAT_READ, StrId::STR_RECENT_STAT_TOTAL,
                                                    StrId::STR_RECENT_STAT_AVERAGE, StrId::STR_RECENT_STAT_DAYS,
                                                    StrId::STR_RECENT_STAT_SPAN};
  HomeStatsInput in;
  in.top = card.coverY;
  in.bottom = card.coverY + card.coverH;
  in.labelLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  in.valueLineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  in.rows = cardStats.rows;
  const auto column = homeStatsLayout(in);
  const int width = card.statsRight - card.statsX;
  for (int row = 0; row < HOME_STAT_COUNT; ++row) {
    if (!(column.rows & (1u << row))) continue;
    renderer.drawText(SMALL_FONT_ID, card.statsX, column.labelY[row], I18N.get(LABELS[row]));
    const char* value = cardStats.values[row].c_str();
    const bool fits = renderer.getTextWidth(UI_12_FONT_ID, value, EpdFontFamily::BOLD) <= width;
    const int font = fits ? UI_12_FONT_ID : UI_10_FONT_ID;
    renderer.drawText(font, card.statsX, column.valueY[row],
                      renderer.truncatedText(font, value, width, EpdFontFamily::BOLD).c_str(), true,
                      EpdFontFamily::BOLD);
  }
  if (column.barY >= 0 && cardStats.recorded) {
    renderer.drawRect(card.statsX, column.barY, width, HOME_STATS_BAR_H);
    renderer.fillRect(card.statsX, column.barY, width * cardStats.percent / 100, HOME_STATS_BAR_H);
    return column.barY;
  }
  return -1;
}

void HomeActivity::drawRecentCard() {
  if (recentBooks.empty()) {
    renderer.drawText(UI_12_FONT_ID, 24, coverTileTop() + 26, tr(STR_NO_RECENT_BOOKS));
    return;
  }
  const int shown = shownRecent();
  const int serifFont = SETTINGS.uiTextSize == 1   ? NOTOSERIF_14_FONT_ID
                        : SETTINGS.uiTextSize == 2 ? NOTOSERIF_16_FONT_ID
                                                   : NOTOSERIF_12_FONT_ID;
  HomeCardInput in;
  in.screenWidth = renderer.getScreenWidth();
  in.top = coverTileTop() - 4;
  // Above both the tip lane and the hint band, which grows with the larger text sizes.
  in.bottom = std::min(tenorchrome::tipY(renderer) + 6,
                       renderer.getScreenHeight() - UITheme::getInstance().getMetrics().buttonHintsHeight);
  in.titleLineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  in.titleLines = 2;
  in.authorLineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  in.excerptLineHeight = renderer.getLineHeight(serifFont);
  in.rowLineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const auto frame = homeCardLayout(in);
#ifdef TENOR_UI_ACCEPTANCE
  const uint32_t restoreStartedUs = micros();
#endif
  if (coverBufferStored && coverBufferBook == shown && restoreCoverBuffer()) {
#ifdef TENOR_UI_ACCEPTANCE
    LOG_INF("HOME_PROBE", "card_restore_us=%lu cache_bytes=%u", static_cast<unsigned long>(micros() - restoreStartedUs),
            static_cast<unsigned>(coverBufferSize));
#endif
    drawCardStats(frame);
    drawOtherBookRow(shown, frame.ruleY, frame.rowY);
    return;
  }
  const uint32_t started = millis();
#ifdef TENOR_UI_ACCEPTANCE
  const uint32_t cardStartedUs = micros();
#endif
  const auto& book = recentBooks[shown];
  const auto title = book.title.empty() ? book.path.substr(book.path.find_last_of('/') + 1) : book.title;
  const auto titleLines = renderer.wrappedText(UI_12_FONT_ID, title.c_str(), frame.textW, 2, EpdFontFamily::BOLD);
  in.titleLines = std::max(1, static_cast<int>(titleLines.size()));
  const auto card = homeCardLayout(in);
  int y = card.titleY;
  for (const auto& line : titleLines) {
    renderer.drawText(UI_12_FONT_ID, card.textX, y, line.c_str(), true, EpdFontFamily::BOLD);
    y += in.titleLineHeight;
  }
  const char* author = book.author.empty() ? tr(STR_RECENT_NO_AUTHOR) : book.author.c_str();
  renderer.drawText(UI_10_FONT_ID, card.textX, card.authorY,
                    renderer.truncatedText(UI_10_FONT_ID, author, card.textW).c_str());
  // Where the build time goes, one mark per step, logged with the build total below.
  uint32_t marks[6];
  marks[0] = millis();
  // A saved quote of the book goes in curly quotes; the page excerpt the reader left on is shown
  // as it is, as in the approved drawings.
  bool quoted = false;
  const auto excerpt = cardExcerpt(shown, quoted);
  marks[1] = millis();
  const bool cjkExcerpt = homeExcerptUsesUiFont(excerpt.c_str());
  const int quoteFont = excerpt.empty() ? SMALL_FONT_ID : cjkExcerpt ? UI_12_FONT_ID : serifFont;
  const auto quoteStyle = excerpt.empty() || cjkExcerpt ? EpdFontFamily::REGULAR : EpdFontFamily::ITALIC;
  const auto quote = excerpt.empty() ? std::string(tr(STR_RECENT_NO_EXCERPT))
                     : quoted        ? std::string("“") + excerpt + "”"
                                     : excerpt;
  // Without a page slot every glyph miss inflates a whole font group again
  // (the decompressor keeps one hot group), which made this card cost ~620 ms
  // on the X3. Prewarm once; the slot is released after the card is cached.
  auto* fcm = renderer.getFontCacheManager();
  if (fcm) fcm->prewarmCache(quoteFont, quote.c_str(), static_cast<uint8_t>(1u << quoteStyle));
  marks[2] = millis();
  y = card.excerptY;
  for (const auto& line : renderer.wrappedText(quoteFont, quote.c_str(), card.textW, card.excerptLines, quoteStyle)) {
    renderer.drawText(quoteFont, card.textX, y, line.c_str(), true, quoteStyle);
    y += renderer.getLineHeight(quoteFont);
  }
  const int textBottom = std::min(y, card.ruleY - 1);
  marks[3] = millis();
  // The reader writes a thumbnail at the card's own height and one at the theme's; the card's is
  // drawn at its own size, the theme's is the fallback for a book not opened since that began.
  bool image = false;
  int coverHeight = 0;
  for (const int height : {HOME_CARD_COVER_H, UITheme::getInstance().getMetrics().homeCoverHeight}) {
    if (image || book.coverBmpPath.empty()) break;
    HalFile file;
    if (!Storage.openFileForRead("HOME", UITheme::getCoverThumbPath(book.coverBmpPath, height), file)) continue;
    Bitmap bitmap(file);
    if (bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() > 0 && bitmap.getHeight() > 0)
      image = renderer.drawBitmapCover(bitmap, card.coverX, card.coverY, card.coverW, card.coverH);
    if (image) coverHeight = height;
  }
  if (!image) {
    for (int yy = 0; yy < card.coverH; ++yy)
      for (int xx = 0; xx < card.coverW; ++xx) {
        const int sx = xx * sleepcover::WIDTH / card.coverW;
        const int sy = yy * sleepcover::HEIGHT / card.coverH;
        const int bit = sy * sleepcover::WIDTH + sx;
        const bool white = (sleepcover::PIXELS[bit / 8] >> (7 - bit % 8)) & 1;
        renderer.drawPixel(card.coverX + xx, card.coverY + yy, !white);
      }
  }
  // Round the cover's corners by painting the page back over them, the pixels a rounded card
  // would not cover: a few hundred pixels, no second buffer, and the cached card keeps them.
  renderer.maskRoundedRectOutsideCorners(card.coverX, card.coverY, card.coverW, card.coverH,
                                         tenorradius::cover(card.coverW));
  marks[4] = millis();
  // Cache the cover and the text block, including typography, only while Home owns them.
  // onPause/onExit release this bounded region before a book or network screen opens.
  coverRectX = card.coverX;
  coverRectY = card.coverY;
  coverRectW = card.coverW;
  coverRectH = card.coverH;
  textRectX = card.textX;
  textRectY = card.titleY;
  textRectW = card.textW;
  textRectH = std::max(0, textBottom - card.titleY);
  coverBufferStored = storeCoverBuffer();
  coverBufferBook = shown;
  coverRendered = true;
  if (fcm) fcm->releaseBuiltinPageCaches();  // the card is now a cached bitmap
  marks[5] = millis();
  loadCardStats(book);
  const int barY = drawCardStats(card);
  drawOtherBookRow(shown, card.ruleY, card.rowY);
#ifdef TENOR_UI_ACCEPTANCE
  LOG_INF("HOME_PROBE", "card_build_us=%lu cache_bytes=%u", static_cast<unsigned long>(micros() - cardStartedUs),
          static_cast<unsigned>(coverBufferSize));
#endif
  const uint32_t finished = millis();
  LOG_INF("HOME", "Recent card build=%lums cache=%u cover=%d title=%lu quote=%lu warm=%lu text=%lu image=%lu keep=%lu",
          static_cast<unsigned long>(finished - started), static_cast<unsigned>(coverBufferSize), coverHeight,
          static_cast<unsigned long>(marks[0] - started), static_cast<unsigned long>(marks[1] - marks[0]),
          static_cast<unsigned long>(marks[2] - marks[1]), static_cast<unsigned long>(marks[3] - marks[2]),
          static_cast<unsigned long>(marks[4] - marks[3]), static_cast<unsigned long>(marks[5] - marks[4]));
  LOG_INF("HOME", "Card stats rows=%02x bar=%d", static_cast<unsigned>(cardStats.rows), barY);
}

// "Another book" and the next book's title under a rule, with an arrow on each side that has a
// book to step to. Drawn on every paint: it is one line of an uncompressed flash font.
void HomeActivity::drawOtherBookRow(const int shown, const int ruleY, const int rowY) {
  const int count = static_cast<int>(recentBooks.size());
  if (count < 2) return;
  constexpr int MARGIN = 40, ARROW_W = 10, ARROW_HALF = 7, ARROW_GAP = 12;
  const int left = MARGIN, right = renderer.getScreenWidth() - MARGIN;
  renderer.drawLine(left, ruleY, right - 1, ruleY);
  const char* label = tr(STR_RECENT_OTHER_BOOK);
  renderer.drawText(UI_10_FONT_ID, left, rowY, label);
  const auto& next = recentBooks[(shown + 1) % count];
  const auto title = next.title.empty() ? next.path.substr(next.path.find_last_of('/') + 1) : next.title;
  const int titleRight = right - ARROW_W - ARROW_GAP;
  const int room = titleRight - left - renderer.getTextWidth(UI_10_FONT_ID, label) - 16;
  const auto shownTitle = renderer.truncatedText(UI_10_FONT_ID, title.c_str(), room, EpdFontFamily::BOLD);
  const int titleWidth = renderer.getTextWidth(UI_10_FONT_ID, shownTitle.c_str(), EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, titleRight - titleWidth, rowY, shownTitle.c_str(), true, EpdFontFamily::BOLD);
  const int cy = rowY + renderer.getFontAscenderSize(UI_10_FONT_ID) * 2 / 3;
  // Solid triangle, tip first; direction 1 points right.
  const auto arrow = [&](const int tipX, const int direction) {
    for (int i = 0; i < ARROW_W; ++i) {
      const int half = i * ARROW_HALF / (ARROW_W - 1);
      renderer.drawLine(tipX - direction * i, cy - half, tipX - direction * i, cy + half);
    }
  };
  arrow(right - 1, 1);
  // Right wraps to the most recent book from the last one, so only the left arrow can be missing.
  if (shown > 0) arrow(left - 24, -1);
}
