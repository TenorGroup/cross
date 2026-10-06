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
#include <RecoverableFile.h>
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
#include "HomeRows.h"
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
#include "UIFontTiers.h"
#include "activities/reader/ReaderActivity.h"
#include "activities/settings/InfoUpdateActivity.h"
#include "activities/settings/SettingsActivity.h"
#include "FileBrowserActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/ReadingStatsFormat.h"
#include "components/ReadingStatsView.h"
#include "util/CoverRef.h"
#include "util/NgayDocXong.h"
#include "components/HomeStatsNavigation.h"
#include "components/CompactStats.h"
#include "components/HomeExcerptStyle.h"
#include "components/SettledListRender.h"
#include "components/TenorMenuChrome.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "components/X3SleepCover.h"
#include "components/themes/TenorRadius.h"
#include "components/icons/tenorHomeTabIcons.h"
#include "fontIds.h"

namespace fui = freeink::ui;

void saveAppState();  // main.cpp

namespace {
constexpr const StrId (&TAB_NAMES)[homerows::PAGE_COUNT] = homerows::PAGE_TITLES;
static_assert(homerows::PAGE_COUNT == HomeActivity::TAB_COUNT, "the two shells list the same five pages");
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

// A card file: this head, then the cover region and the text region as the framebuffer holds them.
// `thumb` is whether the card's own thumbnail was on the card when the cover was drawn: the reader
// writes it the first time it opens a book, and a card drawn before that is stale from then on.
// The cover has a key of its own: leaving a book on a new page changes the excerpt alone, and the
// saved cover then goes back on the card while only the text is laid out again (about 210 ms of a
// 333 ms card on the X3 is the cover). `cover` is the thumbnail height it was drawn from.
struct CardFileHead {
  uint32_t magic;
  uint32_t coverKey;
  uint32_t key;
  int16_t rects[8];
  uint32_t bytes;
  uint8_t thumb;
  int16_t cover;
};
constexpr uint32_t CARD_FILE_MAGIC = 0x34445243;  // "CRD4": cover 298 x 450, one line of excerpt

uint32_t fnv(uint32_t hash, const void* data, size_t size) {
  for (const auto* p = static_cast<const uint8_t*>(data); size--; ++p) hash = (hash ^ *p) * 16777619u;
  return hash;
}
uint32_t fnv(const uint32_t hash, const std::string& text) { return fnv(hash, text.c_str(), text.size() + 1); }

// Books whose missing card thumbnail Home has tried to write since boot, by path hash: a cover that
// fails to decode is not decoded again on every visit.
uint32_t thumbTried[5] = {};
uint8_t thumbTriedNext = 0;
// Idle time on the card before Home decodes a missing cover (1 to 3 s under a notice).
constexpr uint32_t CARD_THUMB_IDLE_MS = 3000;
// Heap a missing thumbnail needs, with the book's index loaded to find the cover.
constexpr size_t CARD_THUMB_MIN_FREE_HEAP = 96 * 1024;
// With the cover's path from cover.ref: the copy out of the book is the peak, 57.516 B on the X3
// (two 8 KB chunks, the 32 KB inflate window and the 8.364 B decompressor), above the decode
// (the 17.884 B JPEG decoder and both thumbnails, about 19 KB for a 900 x 1350 cover). Plus a
// quarter for the other tasks and the allocator. Home sat at 86 to 91 KB after reading, under
// the old 96 KB, so the thumbnail came one visit and not the next.
constexpr size_t CARD_THUMB_REF_MIN_FREE_HEAP = 72 * 1024;
// Copying the cover out of the book inflates through a 32 KB window that must come in one block.
constexpr size_t CARD_THUMB_MIN_LARGEST_BLOCK = 36 * 1024;
}  // namespace

HomeActivity::HomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                           const HomeMenuItem initialMenuItemValue, const bool cleanInitialRefresh)
    : UiTabListActivity("Home", renderer, mappedInput, tenorchrome::kTouchShell),
      initialMenuItem(initialMenuItemValue),
      cleanInitialRefresh(cleanInitialRefresh) {}

freeink::ui::BitmapRef HomeActivity::tabIcon(const int index, const bool bold) const {
  // freeink::Icon dung the Mask1: bit 1 la de trong, bit 0 la ve muc. Doi sang BitmapRef
  // ngay tai cho, vi day la noi duy nhat can phep doi nay.
  static const freeink::Icon* const ANH[TAB_COUNT] = {&icon_tenor_home_recent_40, &icon_tenor_home_folder_40,
                                                      &icon_tenor_home_stats_40, &icon_tenor_home_settings_40,
                                                      &icon_tenor_home_favorites_40};
  static const freeink::Icon* const ANH_DAM[TAB_COUNT] = {
      &icon_tenor_home_recent_bold_40, &icon_tenor_home_folder_bold_40, &icon_tenor_home_stats_bold_40,
      &icon_tenor_home_settings_bold_40, &icon_tenor_home_favorites_bold_40};
  freeink::ui::BitmapRef b;
  if (index < 0 || index >= TAB_COUNT) return b;
  const auto* icon = bold ? ANH_DAM[index] : ANH[index];
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
  app.on(ACTION_OTHER_BOOK, &HomeActivity::otherBookTrampoline, this);
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
    // A book opened since the cursor was kept is the first card now, and the card Home shows.
    const auto found = state.booksOpened != RECENT_BOOKS.opened()
                           ? recentBooks.end()
                           : std::find_if(recentBooks.begin(), recentBooks.end(),
                                          [&](const RecentBook& book) { return book.path == state.selection; });
    activeNav().selected =
        found == recentBooks.end() ? (recentBooks.empty() ? 0 : 1) : static_cast<int>(found - recentBooks.begin()) + 1;
  } else if (activeTabId == Tab::CAI_DAT) {
    if (state.selection == "action/15") activeNav().selected = settingsOrder.display(0) + 1;
    for (size_t i = 0; i < settingsGroups.size(); ++i)
      if (state.selection == "group/" + std::to_string(settingsGroups[i]))
        activeNav().selected = settingsOrder.display(static_cast<int>(i) + 1) + 1;
  }
  activeNav().followOnBuild = true;
}

void HomeActivity::captureNavigation(MenuNavigationState& state) const {
  UiTabListActivity::captureNavigation(state);
  state.selection.clear();
  state.booksOpened = RECENT_BOOKS.opened();
  const int row = ringPos() - 1;
  if (activeTabId == Tab::FAVORITES) state.selection = favoriteKey(row);
  if (activeTabId == Tab::RECENT && row >= 0 && row < static_cast<int>(recentBooks.size()))
    state.selection = recentBooks[row].path;
  if (activeTabId == Tab::CAI_DAT) {
    const int original = settingsOrder.original(row);
    if (original == 0) state.selection = "action/15";
    if (original > 0 && original <= static_cast<int>(settingsGroups.size()))
      state.selection = "group/" + std::to_string(settingsGroups[original - 1]);
  }
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
  settingsOrder = {};

  switch (activeTabId) {
    case Tab::RECENT:
      for (const auto& book : recentBooks) rowLabels.push_back(book.title);
      break;
    case Tab::FOLDER:
      docGocTheNho();
      for (const auto& muc : mucTheNho) {
        // Touch: the File card is the root of File, its rows as a folder's rows (name, icon).
        if (tenorchrome::kTouchShell) {
          char name[128];
          formatFileName(muc, name, sizeof(name));
          rowLabels.emplace_back(name);
        } else {
          rowLabels.push_back(muc);
        }
      }
      break;
    case Tab::STATS: {
      const bool enlarged = statsPaged();
      if (enlarged != statsRowsEnlarged && activeNav().selected > 0) {
        activeNav().selected = statsSelectionForTier(activeNav().selected, statsRowsEnlarged, enlarged);
        activeNav().followOnBuild = true;
      }
      statsRowsEnlarged = enlarged;
      if (enlarged) rowLabels.emplace_back(statsPage ? tr(STR_PREV_PAGE) : tr(STR_NEXT_PAGE));
      for (const StrId id : homerows::STATS_ROWS) rowLabels.emplace_back(I18N.get(id));
      break;
    }
    case Tab::FAVORITES:
      break;
    case Tab::CAI_DAT: {
      rowLabels.emplace_back(tr(STR_FILE_TRANSFER));
      auto groups = homerows::settingsGroups();
      settingsGroups = std::move(groups.ids);
      // X4 Pro: 3 titled groups and the About & updates row after the groups.
      const bool titled = infoupdate::shown();
      settingsOrder = titled ? homesettings::touchOrder(settingsGroups) : homesettings::order(settingsGroups);
      for (auto& label : groups.labels) rowLabels.push_back(std::move(label));
      if (titled) rowLabels.emplace_back(tr(STR_INFO_UPDATES));
      break;
    }
  }

  if (activeTabId == Tab::FAVORITES) {
    auto pins = homerows::favorites();
    favoriteKeys = std::move(pins.keys);
    favoriteValues = std::move(pins.values);
    for (auto& label : pins.labels) rowLabels.push_back(std::move(label));
  }
  rowItems.reserve(rowLabels.size());
  for (size_t i = 0; i < rowLabels.size(); i++) {
    fui::ListItem item;
    item.label = rowLabels[i].c_str();
    item.opensNext = activeTabId == Tab::CAI_DAT || (tenorchrome::kTouchShell && rowOpens(static_cast<int>(i)));
    if (activeTabId == Tab::FAVORITES) item.value = favoriteValues[i].c_str();
    if (tenorchrome::kTouchShell && activeTabId == Tab::FOLDER)
      item.icon = listIconFor(UITheme::getFileIcon(mucTheNho[i]), 32);
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
      if (statsTurnRow(index)) {
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
    case Tab::CAI_DAT: {
      const int original = settingsOrder.original(index);
      if (original < 0) return;
      if (original == 0) {
        activityManager.goToFileTransfer();
        return;
      }
      freeCoverBuffer();
      if (original > static_cast<int>(settingsGroups.size())) {
        if (auto info = makeUniqueNoThrow<InfoUpdateActivity>(renderer, mappedInput))
          startActivityForResult(std::move(info), nullptr);
        return;
      }
      {
        const int group = settingsGroups[original - 1];
        startActivityForResult(makeUniqueNoThrow<SettingsActivity>(renderer, mappedInput, group, true),
                               [this, group](const ActivityResult&) {
                                 RenderLock lock(*this);
                                 rebuildRows();
                                 const auto found = std::find(settingsGroups.begin(), settingsGroups.end(), group);
                                 if (found != settingsGroups.end())
                                   activeNav().selected = settingsOrder.display(
                                       static_cast<int>(found - settingsGroups.begin()) + 1) + 1;
                                 // Touch has no cursor to bring into view: the list stays where it was.
                                 activeNav().followOnBuild = !tenorchrome::kTouchShell;
                               });
      }
      return;
    }
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
  // Touch: a swipe on Stats moves the page (dynamic bar rule 11: a drag by the rows the finger travelled,
  // a flick by a page), a row pitch a step.
  if (statsScrolls()) {
    const int rows = swipeRows(mappedInput, statsRows, listCount(), ACTION_ROW);
    if (rows != 0) {
      RenderLock lock(*this);
      auto& place = tabNavs[static_cast<size_t>(Tab::STATS)];
      const int step = std::clamp(place.top + rows, 0, static_cast<int>(statsView.maxStep));
      if (step != place.top) {
        place.top = step;
        requestUpdate();
      }
      return true;
    }
  }
  // Touch: a sideways swipe on the Recent card steps between books, as the front buttons do.
  if (tenorchrome::kTouchShell && activeTabId == Tab::RECENT) {
    const auto swipe = mappedInput.wasSwipe();
    if (swipe == MappedInputManager::SwipeDir::Left || swipe == MappedInputManager::SwipeDir::Right) {
      queueNavIntent(swipe == MappedInputManager::SwipeDir::Left ? NavIntent::StepNext : NavIntent::StepPrev);
      return true;
    }
  }
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
        cardRecordsRead = cardRecordsFound = 0;
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
  else  // Home is the stack root: no back button in its header.
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding - metrics.topPadding},
                   tabLabel(activeTab()), nullptr, false);

  // Touch: the panel is part of the page buildStatsPage scrolls.
  if (activeTabId == Tab::STATS && !statsScrolls()) drawStatsPanel(coverTileTop() + 12);
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
  return tenorchrome::enabled() ? tenorchrome::tabTop() : metrics.topPadding + metrics.headerHeight;
}

int HomeActivity::preferredTabBarHeight() const {
  if (tenorchrome::enabled()) {
    // Include 2 px above, 4 below and the divider: the filled box equals a row.
    return tenorchrome::tabHeight();
  }
  return UiTabListActivity::preferredTabBarHeight();
}

int HomeActivity::coverTileTop() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  if (!tenorchrome::enabled())
    return metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight;
  return tabBarTop() + (tenorchrome::kTouchShell ? 0 : preferredTabBarHeight()) + 16;
}

void HomeActivity::settingsRow(void* context, const uint16_t index, fui::ListItem& item) {
  auto* self = static_cast<HomeActivity*>(context);
  const int original = self->settingsOrder.original(index);
  if (original < 0 || original >= static_cast<int>(self->rowItems.size())) return;
  item = self->rowItems[original];
  if (original == 0 && self->rowIsPinned(index)) {
    self->settingsTransferLabel = "\xEE\x84\x8A";
    self->settingsTransferLabel += item.label ? item.label : "";
    item.label = self->settingsTransferLabel.c_str();
  }
  item.actionValue = static_cast<int16_t>(index);
  const int heading = self->settingsOrder.heading(index);
  if (heading >= 0) item.sectionHeading = I18N.get(homesettings::TOUCH_HEADINGS[heading]);
}

bool HomeActivity::buildSettingsGroups(UiScreen& screen) {
  // Touch: the titled groups do not fit a page; they scroll as one framed list, a frame a group.
  if (infoupdate::shown() || renderer.getOrientation() != GfxRenderer::Orientation::Portrait) return false;
  reserveFixedMenuContent(screen);
  const auto body = screen.body();
  const auto style = uiMenuLabelText(screen.theme());
  const auto geometry = homesettings::layout({body.x, body.y, body.width, body.height}, settingsOrder,
      tenorchrome::kTouchShell, normalizedUiTextSize(SETTINGS.uiTextSize), screen.target().lineHeight(style.font));
  if (!geometry.fits) return false;

  fui::ListProps props;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch | (tenorchrome::kTouchShell ? fui::InputLongPress : 0);
  props.labelText = style;
  props.labelText.maxLines = 2;
  props.rowHeight = static_cast<int16_t>(geometry.rowHeight);
  props.rowGap = static_cast<int16_t>(geometry.rowGap);
  props.rowPaddingY = static_cast<int16_t>(geometry.rowPaddingY);
  props.rowInset = 0;
  props.sidePadding = 12;
  props.scrollIndicator = false;
  props = screen.resolveListProps(props);
  if (tenorchrome::kTouchShell) props.rowStyles.selected = props.rowStyles.normal;
  // A wider translated or custom-font label keeps the ordinary scrolling list.
  for (int i = 0; i < settingsOrder.count; ++i) {
    fui::ListItem item;
    settingsRow(this, static_cast<uint16_t>(i), item);
    if (fui::measureListRow(screen.target(), nullptr, static_cast<int16_t>(geometry.rows[0].width), props, item).height >
        geometry.rowHeight) return false;
  }

  auto& n = activeNav();
  clampAfterNav();
  n.syncToProps(body, 1, 0, settingsOrder.count, props, 1);
  n.top = 0;
  n.visibleRows = settingsOrder.count;
  const int selected = props.selectedIndex;
  struct Rows { HomeActivity* home; int offset; };
  int offset = 0;
  for (int group = 0; group < 2; ++group) {
    const auto& frame = geometry.frames[group];
    const auto& rows = geometry.rows[group];
    const int count = group == 0 ? settingsOrder.readingCount : settingsOrder.count - settingsOrder.readingCount;
    if (count == 0) continue;
    // Touch: the ring and the grey rules of every framed list, so each row has the same height between its lines.
    // The button readers keep their groups unframed (founder 06/10/2026).
    if (tenorchrome::roundFrames()) {
      const auto lines = rowFrameLines(geometry.rowGap);
      renderer.drawRoundedRect(frame.x, rows.y - lines.top, frame.width, rows.height + lines.top + lines.bottom, 2,
                               geometry.radius, true);
      for (int k = 1; k < count; ++k)
        tenorchrome::drawRowRule(renderer, rows.y + k * (geometry.rowHeight + geometry.rowGap) - lines.rule,
                                 frame.x + 16, frame.x + frame.width - 17);
    }
    Rows context{this, offset};
    props.rowProviderCtx = &context;
    props.rowProvider = [](void* user, const uint16_t index, fui::ListItem& item) {
      const auto* context = static_cast<const Rows*>(user);
      settingsRow(context->home, static_cast<uint16_t>(context->offset + index), item);
    };
    props.count = static_cast<uint16_t>(count);
    props.topIndex = 0;
    props.selectedIndex = tenorchrome::kTouchShell ? int16_t{-1} : static_cast<int16_t>(selected - offset);
    props.nav = nullptr;  // Both blocks share the Home tab's one cursor.
    fui::list(screen.frame(), {static_cast<int16_t>(rows.x), static_cast<int16_t>(rows.y),
                               static_cast<int16_t>(rows.width), static_cast<int16_t>(rows.height)}, props);
    offset += count;
  }
  n.onListRendered(0, settingsOrder.count, selected < 0 || selected < settingsOrder.count, selected);
  n.drawnCount = settingsOrder.count;
  return true;
}

void HomeActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(tabBarTop()), 0, static_cast<int16_t>(metrics.buttonHintsHeight), 0});

  buildTabBar(screen);
  if (activeTabId == Tab::CAI_DAT && buildSettingsGroups(screen)) return;
  if (statsScrolls()) {
    buildStatsPage(screen);
    return;
  }
  if (activeTabId == Tab::RECENT && tenorchrome::enabled()) {
    // drawChrome() paints the whole card; no list rows are drawn. The ring still counts one row
    // per book, so the front buttons walk the books with the usual wrap, and the viewport spans
    // them all: no "more below" chevron, and holding a front button reaches the first or last.
    auto& n = activeNav();
    const int count = listCount();
    n.selected = count > 0 ? std::clamp(n.selected.load(), 1, count) : 0;
    n.top = 0;
    n.visibleRows = std::max(1, count);
    n.drawnRows = count;
    n.drawnCount = count;
    n.followOnBuild = false;
    n.followPending = false;
    // Touch: the whole card opens the book it shows.
    if (tenorchrome::kTouchShell && count > 0) {
      const fui::Rect body = screen.body();
      // A hold pins or unpins the book shown, like holding Select on the button readers.
      screen.frame().hit(body, ACTION_ROW, static_cast<int16_t>(shownRecent()), fui::InputTouch | fui::InputLongPress);
      // The "other books" line steps to the next book instead of opening the one shown.
      if (count > 1) {
        constexpr int16_t STRIP = 56;
        screen.frame().hit(fui::Rect{body.x, static_cast<int16_t>(body.y + body.height - STRIP), body.width, STRIP},
                           ACTION_OTHER_BOOK, 0, fui::InputTouch | fui::InputLongPress);
      }
    }
    return;
  }
  // Leave the cover tile's band to drawChrome(); the list starts under it.
  if (activeTabId == Tab::RECENT) screen.takeTop(static_cast<int16_t>(metrics.homeCoverTileHeight));

  if (activeTabId == Tab::STATS) {
    // Keep the panel's 12 px top inset and 8 px of white space before the list border.
    screen.takeTop(static_cast<int16_t>(statsPanelHeight() + 12 + 8));
  }

  if (activeTabId == Tab::FAVORITES && rowItems.empty()) {
    // The tip names front buttons, which the touch shell has none of.
    if (tenorchrome::kTouchShell) {
      // Up to 3 lines, centred in the body: the sentence is longer than one line of the body font.
      fui::TextStyle style = screen.theme().bodyText;
      style.align = fui::TextAlign::Center;
      style.maxLines = 3;
      const fui::Rect body = screen.body();
      const int16_t height = static_cast<int16_t>(3 * screen.target().lineHeight(style.font));
      screen.target().text(fui::Rect{static_cast<int16_t>(body.x + 32), static_cast<int16_t>(body.y + (body.height - height) / 2),
                                     static_cast<int16_t>(body.width - 64), height},
                           tr(STR_FAVORITES_EMPTY_TOUCH), style);
    } else
      tenorchrome::drawTip(renderer, tr(STR_HOME_FAVORITES_HINT), 0, 6);
    return;
  }
  if (activeTabId == Tab::FOLDER && rowItems.empty()) {
    screen.centeredText(mucQuaNhieu ? tr(STR_FOLDER_TOO_MANY) : tr(STR_NO_FILES_FOUND), screen.theme().bodyText);
    return;
  }

  if (favoriteFileMissing)
    screen.takeBottom(static_cast<int16_t>(28 + tenorchrome::tipHeight(renderer, tr(STR_FILE_NOT_FOUND), 3)));
  if (activeTabId == Tab::STATS && statsResetTip)
    screen.takeBottom(static_cast<int16_t>(28 + tenorchrome::tipHeight(renderer, I18N.get(*statsResetTip), 2)));
  fui::ListProps props;
  props.items = rowItems.data();
  if (activeTabId == Tab::CAI_DAT) {
    props.items = nullptr;
    props.rowProvider = &HomeActivity::settingsRow;
    props.rowProviderCtx = this;
    // The titles of the X4 Pro groups, over their frames.
    props.headerText = screen.theme().smallText;
    props.headerUnderline = false;
  }
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  if (tenorchrome::kTouchShell) props.inputMask |= fui::InputLongPress;  // hold a row to pin it
  props.labelText = uiMenuLabelText(screen.theme());
  props.labelText.maxLines = 2;
  syncTabListViewport(screen, props);
  screen.list(props);
}

void HomeActivity::render(RenderLock&&) {
  if (rowMenu.processRender(renderer, mappedInput)) return;
  if (activeTabId == Tab::STATS && statsRowsEnlarged != statsPaged()) rebuildRows();
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
    if (statsScrolls()) drawStatsEdges();
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
  const unsigned long frameMs = millis() - started;
  if (!cardFilePending.empty()) saveCardFile();
  // The card file holds what the snapshot holds, so the RAM copy goes: 23.706 B on the X3 against
  // 19.215 B of the 236 x 356 card, free heap 72.280 B against 77.392 B. A repaint reads the file.
  if (cardOnCard && tenorchrome::enabled()) freeCoverBuffer();
  // top: the tab's viewport, on touch Stats the page's step.
  const int top = statsScrolls() ? tabNavs[static_cast<size_t>(Tab::STATS)].top : activeNav().top;
  LOG_INF("HOME", "Frame row=%d top=%d total=%lums heap=%u held=%u largest=%u", ringPos(), top, frameMs,
          ESP.getFreeHeap(), static_cast<unsigned>(coverBufferSize), static_cast<unsigned>(ESP.getMaxAllocHeap()));
  // The wake's first frame is up: the main task writes the splash setup() re-armed (onTick).
  if (cleanInitialRefresh) wakeStatePending = true;
  cleanInitialRefresh = false;
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
  cardOnCard = false;
}

void HomeActivity::loadRecentBooks() { recentBooks = homerows::recent(RECENT_LIMIT); }

void HomeActivity::docGocTheNho() {
  // Vung nho hung ten file muon roi tra ngay trong ham nay. Giu no song suot doi man
  // chinh la an them RAM ma khong lam cuon sach de doc hon.
  constexpr size_t DEM_CO = 500;  // bang FileBrowserActivity::NAME_BUFFER_SIZE
  auto dem = makeUniqueNoThrow<char[]>(DEM_CO);
  if (!dem) {
    mucTheNho.clear();
    return;
  }
  // Past the ceiling the heap allows the root is refused whole (see docthumuc::tran).
  const size_t cap = docthumuc::tran(ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  mucQuaNhieu = cap == 0;
  if (cap == 0) {
    mucTheNho.clear();
    return;
  }
  docthumuc::doc("/", SETTINGS.showHiddenFiles, docthumuc::Loc::Sach, dem.get(), DEM_CO, mucTheNho, cap, &mucQuaNhieu);
}

bool HomeActivity::rowStartsGroup(const int row) const {
  return activeTabId == Tab::CAI_DAT && settingsOrder.heading(row) >= 0;
}

bool HomeActivity::rowOpens(const int row) const {
  // Settings groups and the stats screens open a screen; a folder opens its rows; a book opens the book.
  // The enlarged stats tab's page row turns the page in place.
  if (activeTabId == Tab::STATS) return !statsTurnRow(row);
  if (activeTabId == Tab::CAI_DAT) return true;
  return activeTabId == Tab::FOLDER && row >= 0 && row < static_cast<int>(mucTheNho.size()) &&
         !mucTheNho[row].empty() && mucTheNho[row].back() == '/';
}

void HomeActivity::onRowLongPress(const int index) {
  UiListActivity::onRowLongPress(index);
  if (activeTabId != Tab::RECENT || !rowMenu.isActive() || heldCover.height <= 0) return;
  const StrId label = rowIsPinned(index) ? StrId::STR_UNPIN_FAVORITE : StrId::STR_PIN_FAVORITE;
  rowMenu.showAnchored(heldCover, &label, 1, [this, index](int) { queuePinToggle(index); });
}

void HomeActivity::showOtherBookMenu() {
  const int count = static_cast<int>(recentBooks.size());
  if (count < 2) return;
  const int next = (shownRecent() + 1) % count;
  const StrId label = rowIsPinned(next) ? StrId::STR_UNPIN_FAVORITE : StrId::STR_PIN_FAVORITE;
  showRowMenu(&label, 1, [this, next](int) { queuePinToggle(next); }, ACTION_OTHER_BOOK, 0);
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
  if (activeTabId == Tab::CAI_DAT && settingsOrder.original(row) == 0) return "action/15";
  if (activeTabId != Tab::FAVORITES || row < 0 || row >= static_cast<int>(favoriteKeys.size())) return {};
  return favoriteKeys[row];
}
int HomeActivity::focusFavorite(const std::string& key) {
  if (activeTabId != Tab::CAI_DAT || key != "action/15") return UiListActivity::focusFavorite(key);
  const int row = settingsOrder.display(0);
  if (row < 0) return -1;
  RenderLock lock(*this);
  activeNav().selected = row + 1;
  activeNav().followOnBuild = true;
  return row;
}
void HomeActivity::favoritesChanged() {
  const int selected = activeNav().selected;
  rebuildRows();
  activeNav().selected = std::min(selected, listCount());
  if (listCount() && activeNav().selected <= 0) activeNav().selected = 1;
  // Touch has no cursor to bring into view: the list keeps its place.
  activeNav().followOnBuild = !tenorchrome::kTouchShell;
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

bool HomeActivity::statsPaged() const {
  return !tenorchrome::kTouchShell && normalizedUiTextSize(SETTINGS.uiTextSize) != 0;
}

int HomeActivity::statsPanelHeight() const {
  if (normalizedUiTextSize(SETTINGS.uiTextSize) == 0) return readingstatsview::HEIGHT;
  return readingstatsview::panelHeight(renderer, statsPaged() ? statsPage : -1) +
         (habitSuggestion() ? renderer.getLineHeight(UI_12_FONT_ID) + 8 : 0);
}

void HomeActivity::drawStatsPanel(const int top) {
  const auto* suggestion = habitSuggestion();
  if (suggestion) {
    const int pageWidth = renderer.getScreenWidth();
    renderer.drawText(UI_12_FONT_ID, 24, top,
                      renderer.truncatedText(UI_12_FONT_ID, suggestion, pageWidth - 48, EpdFontFamily::BOLD).c_str(),
                      true, EpdFontFamily::BOLD);
  }
  const bool enlarged = normalizedUiTextSize(SETTINGS.uiTextSize) != 0;
  const int badge = enlarged ? renderer.getLineHeight(UI_12_FONT_ID) + 8 : readingstatsview::BADGE_HEIGHT;
  readingstatsview::draw(renderer, top + (suggestion ? badge : 0), false, suggestion, statsPaged() ? statsPage : -1);
}

fui::ListNav& HomeActivity::activeNav() {
  if (statsScrolls()) return statsRows;
  return UiTabListActivity::activeNav();
}

UiListActivity::RowFrameStyle HomeActivity::rowFrameStyle() const {
  if (!statsScrolls()) return {};
  // The rows' frame is every list's frame, like the panels above it; the page's view cuts it.
  RowFrameStyle style;
  style.clipTop = statsView.top;
  style.clipBottom = statsView.bottom;
  return style;
}

void HomeActivity::buildStatsPage(UiScreen& screen) {
  if (statsResetTip)
    screen.takeBottom(static_cast<int16_t>(28 + tenorchrome::tipHeight(renderer, I18N.get(*statsResetTip), 2)));
  const fui::Rect body = screen.body();
  StatsView& view = statsView;
  // Under the status strip; over the bar by the gap every touch list keeps (8 px).
  view.top = static_cast<int16_t>(tabBarTop());
  view.bottom = static_cast<int16_t>(
      std::min<int>(body.y + body.height, tenorchrome::touchBarTop(renderer.getScreenHeight()) - 8));
  const int count = std::min(listCount(), homerows::STATS_ROW_COUNT);

  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(count);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch | fui::InputLongPress;
  props.labelText = uiMenuLabelText(screen.theme());
  props.labelText.maxLines = 2;
  frameRows(props);
  props = screen.resolveListProps(props);
  // Every row measured before any is drawn, so the page knows where it ends.
  const int16_t rowWidth = static_cast<int16_t>(renderer.getScreenWidth() - 2 * props.rowInset);
  int rowTops[homerows::STATS_ROW_COUNT] = {};
  int rowsHeight = 0;
  for (int i = 0; i < count; ++i) {
    rowTops[i] = rowsHeight;
    rowsHeight += fui::measureListRow(screen.target(), screen.frame().assets(), rowWidth, props, rowItems[i]).height +
                  (i + 1 < count ? props.rowGap : 0);
  }
  const int pitch = count > 1 ? rowTops[1] : std::max(1, rowsHeight);
  // The rows' ring stands round them as on every framed list: its top 8 px under the panels, its foot on the
  // view's foot at the end of the page.
  rowFrameGap = props.rowGap;
  const auto lines = rowFrameLines(rowFrameGap);
  const int panelTop = coverTileTop() + 12;
  const int listTop = panelTop + statsPanelHeight() + 8 + lines.top;
  const int viewHeight = view.bottom - view.top;
  const int maxOffset = std::max(0, listTop + rowsHeight + lines.bottom - view.bottom);
  view.maxStep = static_cast<int16_t>((maxOffset + pitch - 1) / pitch);
  auto& place = tabNavs[static_cast<size_t>(Tab::STATS)];
  place.top = std::clamp(place.top, 0, static_cast<int>(view.maxStep));
  view.offset = static_cast<int16_t>(std::min(place.top * pitch, maxOffset));
  view.length = static_cast<int16_t>(viewHeight + maxOffset);

  const auto clip = renderer.getClipRect();
  renderer.setClipRect(0, view.top, renderer.getScreenWidth(), viewHeight);
  drawStatsPanel(panelTop - view.offset);
  renderer.setClipRect(clip[0], clip[1], clip[2], clip[3]);

  // The rows from the first wholly under the view's top; the SDK cuts the one the view's foot cuts and
  // gives it no hit, so a tap only takes a row wholly in view.
  int first = 0;
  while (first < count - 1 && listTop - view.offset + rowTops[first] < view.top) ++first;
  const int y = listTop - view.offset + rowTops[first];
  props.topIndex = static_cast<uint16_t>(first);
  props.nav = nullptr;  // the page owns the scroll; statsRows is told what was laid out below
  rowFrameFloor = view.bottom;
  fui::list(screen.frame(),
            {0, static_cast<int16_t>(y), static_cast<int16_t>(renderer.getScreenWidth()),
             static_cast<int16_t>(view.bottom - y)},
            props);
  // Every row counts as on the page, so drawRowFrame closes the frame round all of them.
  statsRows.onListRendered(static_cast<uint16_t>(first), count - first, true);
  statsRows.drawnCount = count;
}

void HomeActivity::drawStatsEdges() {
  const StatsView& view = statsView;
  const int viewHeight = view.bottom - view.top;
  if (view.length <= viewHeight) return;
  // Rule 7: the edges fade where the page goes on.
  constexpr int BAND = 64;
  if (view.offset > 0) tenorchrome::fadeBand(renderer, view.top, BAND, true);
  if (view.offset < view.length - viewHeight) tenorchrome::fadeBand(renderer, view.bottom - BAND, BAND, false);
  // Rule 13: a pill in the screen's right margin, outside every frame (founder 06/10). The SDK bar gives
  // round ends to a bar in a round frame; this one's "frame" is 1 px rounder than its inset, so its ends
  // only keep 5 px of air.
  constexpr int16_t WIDTH = 5, INSET = 5;
  fui::drawListScrollIndicator(uiTarget, fui::Rect{0, view.top, static_cast<int16_t>(renderer.getScreenWidth()),
                                                   static_cast<int16_t>(viewHeight)},
                               view.length, viewHeight, view.offset, WIDTH, 0, INSET, INSET + 1);
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

// The expected finish as the card's column writes it: a day and month, or the few words that stand in
// for one. False when the record is too thin to estimate.
static bool cardFinishText(const BookReadingRecord& record, char* text, const size_t size) {
  const uint32_t today = ReadingStatsStore::currentDay();
  const auto u = ngaydocxong::uocTinh(record.progress, record.startProgress, record.days, record.firstDay,
                                      record.lastDay, today);
  switch (u.trangThai) {
    case ngaydocxong::TrangThai::ChuaDu:
      return false;
    case ngaydocxong::TrangThai::DaXong:
      snprintf(text, size, "%s", tr(STR_STATS_FINISHED));
      return true;
    case ngaydocxong::TrangThai::QuaXa: {
      char days[24];
      snprintf(days, sizeof(days), tr(STR_RECENT_STAT_DAYS_VALUE), static_cast<unsigned>(ngaydocxong::TRAN_NGAY));
      snprintf(text, size, ">%s", days);
      return true;
    }
    case ngaydocxong::TrangThai::SoNgay:
      snprintf(text, size, tr(STR_RECENT_STAT_DAYS_VALUE), static_cast<unsigned>(u.soNgay));
      return true;
    case ngaydocxong::TrangThai::NgayCuThe:
      compactstats::dayMonth(u.ngay, text, size);
      return true;
  }
  return false;
}

void HomeActivity::loadCardStats(const int index) {
  const uint8_t bit = static_cast<uint8_t>(1u << index);
  if (!(cardRecordsRead & bit)) {
    cardRecordsRead |= bit;
    if (READING_STATS.readBook(recentBooks[index].path, cardRecords[index])) cardRecordsFound |= bit;
  }
  const BookReadingRecord& record = cardRecords[index];
  cardStats = {};
  cardStats.recorded = cardRecordsFound & bit;
  const uint64_t elapsed = static_cast<uint64_t>(record.minutes) * 60000 + record.remainderMs;
  auto& values = cardStats.values;
  char text[64];
  const bool finish = cardStats.recorded && cardFinishText(record, text, sizeof(text));
  if (finish) values[HOME_STAT_FINISH] = text;
  cardStats.rows = homeStatRows(cardStats.recorded, elapsed, record.days, record.turns, finish);
  cardStats.percent = std::min<uint8_t>(record.progress, 100);
  if (!cardStats.recorded) {
    values[HOME_STAT_READ] = tr(STR_STATS_NOT_RECORDED);
    return;
  }
  snprintf(text, sizeof(text), "%u%%", static_cast<unsigned>(cardStats.percent));
  values[HOME_STAT_READ] = text;
  compactstats::duration(elapsed / 60000, text, sizeof(text));
  values[HOME_STAT_TOTAL] = text;
  if (record.days) {
    compactstats::duration(elapsed / record.days / 60000, text, sizeof(text));
    char perDay[64];
    snprintf(perDay, sizeof(perDay), tr(STR_RECENT_STAT_PER_DAY), text);
    values[HOME_STAT_AVERAGE] = perDay;
    snprintf(text, sizeof(text), "%u", static_cast<unsigned>(record.days));
    values[HOME_STAT_DAYS] = text;
  }
  snprintf(text, sizeof(text), "%u", static_cast<unsigned>(record.turns));
  values[HOME_STAT_TURNS] = text;
}

static int cardStatRunWidth(const GfxRenderer& renderer, const int font, const std::string& part,
                            const EpdFontFamily::Style style) {
  return std::max(renderer.getTextWidth(font, part.c_str(), style),
                  renderer.getTextAdvanceX(font, part.c_str(), style));
}

static int cardStatValueWidth(const GfxRenderer& renderer, const int font, const char* value) {
  int width = 0;
  compactstats::runs(value, [&](const std::string& part, const bool number) {
    width += cardStatRunWidth(renderer, font, part, number ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
  });
  return width;
}

static constexpr StrId CARD_STAT_LABELS[HOME_STAT_COUNT] = {
    StrId::STR_RECENT_STAT_READ, StrId::STR_RECENT_STAT_FINISH, StrId::STR_RECENT_STAT_TOTAL,
    StrId::STR_RECENT_STAT_AVERAGE, StrId::STR_RECENT_STAT_DAYS, StrId::STR_STATS_TURNS};

// Label in the subtitle face, value in the body face (bold), stepping down one size when the value is
// wider than the column, as in the approved drawing. Returns the top of the progress bar, -1 when
// none is drawn. Six groups extend to the cover edge when their record has data.
int HomeActivity::drawCardStats(const HomeCardLayout& card) {
  const bool secondary = !tenorchrome::kTouchShell && SETTINGS.uiTextSize == 2;
  const int labelFont = secondary ? SMALL_FONT_ID : UI_10_FONT_ID;
  const int valueFont = secondary ? UI_10_FONT_ID : UI_12_FONT_ID;
  const int fallbackFont = secondary ? SMALL_FONT_ID : UI_10_FONT_ID;
  const int width = card.statsRight - card.statsX;
  HomeStatsInput in;
  in.top = card.statsTop;
  in.bottom = card.coverY + card.coverH;
  in.labelLineHeight = renderer.getLineHeight(labelFont);
  in.valueLineHeight = renderer.getLineHeight(valueFont);
  in.valueTail = in.valueLineHeight - renderer.getFontAscenderSize(valueFont);
  in.rows = cardStats.rows;
  int fonts[HOME_STAT_COUNT];
  std::string first[HOME_STAT_COUNT], second[HOME_STAT_COUNT];
  for (int row = 0; row < HOME_STAT_COUNT; ++row) {
    if (!(in.rows & (1u << row))) continue;
    first[row] = cardStats.values[row];
    fonts[row] = cardStatValueWidth(renderer, valueFont, first[row].c_str()) <= width ? valueFont : fallbackFont;
    if (cardStatValueWidth(renderer, fonts[row], first[row].c_str()) > width) {
      // Long durations keep every number and unit, splitting at the duration space or /day suffix.
      const auto slash = first[row].find('/');
      const auto space = first[row].find(' ');
      const size_t split = slash != std::string::npos &&
                                   cardStatValueWidth(renderer, fonts[row], first[row].substr(0, slash).c_str()) <= width
                               ? slash : space;
      if (split != std::string::npos) {
        second[row] = first[row].substr(split + (first[row][split] == ' ' ? 1 : 0));
        first[row].resize(split);
      }
    }
    const int lineHeight = renderer.getLineHeight(fonts[row]);
    in.valueHeights[row] = lineHeight * (second[row].empty() ? 1 : 2);
    int inkBottom = 0;
    const auto& lastLine = second[row].empty() ? first[row] : second[row];
    compactstats::runs(lastLine.c_str(), [&](const std::string& part, const bool number) {
      inkBottom = std::max(inkBottom, renderer.getTextInkBottom(
          fonts[row], part.c_str(), number ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR));
    });
    in.valueTails[row] = lineHeight - inkBottom;
  }
  const auto column = homeStatsLayout(in);
  for (int row = 0; row < HOME_STAT_COUNT; ++row) {
    if (!(column.rows & (1u << row))) continue;
    renderer.drawText(labelFont, card.statsX, column.labelY[row], I18N.get(CARD_STAT_LABELS[row]));
    const auto drawValue = [&](const std::string& value, const int y) {
      int x = card.statsX;
      compactstats::runs(value.c_str(), [&](const std::string& part, const bool number) {
        const auto style = number ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
        renderer.drawText(fonts[row], x, y, part.c_str(), true, style);
        x += cardStatRunWidth(renderer, fonts[row], part, style);
      });
    };
    drawValue(first[row], column.valueY[row]);
    if (!second[row].empty()) drawValue(second[row], column.valueY[row] + renderer.getLineHeight(fonts[row]));
  }
  if (column.barY >= 0 && cardStats.recorded) {
    // A round track with a 2 px edge; the part read is black inside it, round at both ends.
    constexpr int EDGE = 2;
    renderer.drawRoundedRect(card.statsX, column.barY, width, HOME_STATS_BAR_H, EDGE, HOME_STATS_BAR_H / 2, true);
    const int read = (width - 2 * EDGE) * cardStats.percent / 100;
    if (read > 0)
      renderer.fillRoundedRect(card.statsX + EDGE, column.barY + EDGE, read, HOME_STATS_BAR_H - 2 * EDGE,
                               (HOME_STATS_BAR_H - 2 * EDGE) / 2, Color::Black);
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
  loadCardStats(shown);  // uses the existing per-book record cache before geometry is measured
  const int serifFont = SETTINGS.uiTextSize == 1   ? NOTOSERIF_14_FONT_ID
                        : SETTINGS.uiTextSize == 2 ? NOTOSERIF_16_FONT_ID
                                                   : NOTOSERIF_12_FONT_ID;
  HomeCardInput in;
  in.screenWidth = renderer.getScreenWidth();
  in.top = coverTileTop() - 4;  // Chrome owns the touch top inset.
  in.metadataAboveCover = SETTINGS.uiTextSize > 0;
  // Above both the tip lane and the hint band, which grows with the larger text sizes.
  in.bottom = std::min(tenorchrome::tipY(renderer) + 6,
                       renderer.getScreenHeight() - UITheme::getInstance().getMetrics().buttonHintsHeight);
  in.titleLineHeight = renderer.getLineHeight(UI_TITLE_FONT_ID);  // the cover is sized for the title's own face
  in.titleLines = 2;
  in.authorLineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  in.excerptLineHeight = renderer.getLineHeight(serifFont);
  in.rowLineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int statLabelFont = !tenorchrome::kTouchShell && SETTINGS.uiTextSize == 2 ? SMALL_FONT_ID : UI_10_FONT_ID;
  const int statValueFont = !tenorchrome::kTouchShell && SETTINGS.uiTextSize == 2 ? UI_10_FONT_ID : UI_12_FONT_ID;
  for (const auto label : CARD_STAT_LABELS)
    in.statsMinWidth = std::max(in.statsMinWidth, renderer.getTextWidth(statLabelFont, I18N.get(label)));
  // Stable per board/tier: a minute changing never changes cover geometry or a saved-card key.
  in.statsMinWidth = std::max(in.statsMinWidth, cardStatValueWidth(renderer, statValueFont, "999h 59m"));
  char averageSample[64];
  snprintf(averageSample, sizeof(averageSample), tr(STR_RECENT_STAT_PER_DAY), SETTINGS.uiTextSize ? "999h 59m" : "1h 21m");
  in.statsMinWidth = std::max(in.statsMinWidth, cardStatValueWidth(renderer, statValueFont, averageSample));
  in.statsMinWidth += 8;
  // A fixed budget for two wrapped duration values leaves geometry independent of book time.
  in.minStatsHeight = 2 + HOME_STAT_COUNT * (renderer.getLineHeight(statLabelFont) +
                                           renderer.getLineHeight(statValueFont)) + 12 +
                      2 * std::max(0, 2 * renderer.getLineHeight(!tenorchrome::kTouchShell && SETTINGS.uiTextSize == 2 ? SMALL_FONT_ID : UI_10_FONT_ID) -
                                      renderer.getLineHeight(statValueFont));
  const auto frame = homeCardLayout(in);
  heldCover = {static_cast<int16_t>(frame.coverX), static_cast<int16_t>(frame.coverY), static_cast<int16_t>(frame.coverW),
               static_cast<int16_t>(frame.coverH)};
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
  const char* author = book.author.empty() ? tr(STR_RECENT_NO_AUTHOR) : book.author.c_str();
  // A saved quote of the book goes in curly quotes; the page excerpt the reader left on is shown
  // as it is, as in the approved drawings.
  bool quoted = false;
  const auto excerpt = cardExcerpt(shown, quoted);
  const auto quote = excerpt.empty() ? std::string(tr(STR_RECENT_NO_EXCERPT))
                     : quoted        ? std::string("“") + excerpt + "”"
                                     : excerpt;
#ifdef TENOR_PRESS_PROBE
  const uint32_t quotedMs = millis();
#endif
  // The cover and the lines under it depend on these alone (the cover's height and the title's
  // line count do not move the cover), so the keys say whether a card file still shows this card,
  // or at least its cover.
  const std::string thumbPath =
      book.coverBmpPath.empty() ? std::string() : UITheme::getCoverThumbPath(book.coverBmpPath, HOME_CARD_COVER_H);
  const int16_t geometry[] = {static_cast<int16_t>(frame.coverX), static_cast<int16_t>(frame.coverY),
                              static_cast<int16_t>(frame.coverW), static_cast<int16_t>(frame.coverH),
                              static_cast<int16_t>(frame.textX),  static_cast<int16_t>(frame.titleY),
                              static_cast<int16_t>(frame.textW),  static_cast<int16_t>(SETTINGS.uiTextSize),
                              static_cast<int16_t>(SETTINGS.screenInverted)};
  const uint32_t coverKey = fnv(fnv(2166136261u, CROSSPOINT_VERSION), geometry, sizeof(geometry));
  const uint32_t key =
      fnv(fnv(fnv(fnv(coverKey, title), std::string(author)), quote), std::string(SETTINGS.sdFontFamilyName));
  // One file for the page excerpt and four for quotes, so a book with a few quotes finds its card
  // whichever the visit picked, and a book's files stay bounded however many it has.
  std::string cardPath;
  if (!thumbPath.empty()) {
    cardPath = thumbPath + ".card";
    cardPath += quoted ? static_cast<char>('0' + (static_cast<uint32_t>(cardQuotes[shown]) * 2654435761u >> 30)) : 'e';
  }
  const CardFile saved = cardPath.empty() ? CardFile::None : loadCardFile(cardPath, coverKey, key, thumbPath);
  if (saved == CardFile::Whole && restoreCoverBuffer()) {
    coverBufferBook = shown;
    coverRendered = true;
    if (!cardFileThumb) wantThumb(shown);
    drawCardStats(frame);
    drawOtherBookRow(shown, frame.ruleY, frame.rowY);
    LOG_INF("HOME", "Recent card file=%lums cache=%u", static_cast<unsigned long>(millis() - started),
            static_cast<unsigned>(coverBufferSize));
    return;
  }
  // Only the text changed: the saved cover goes back, the snapshot memory goes to the text layout.
  int coverHeight = 0;
  const bool coverKept = saved == CardFile::Cover && restoreCoverBuffer();
  if (coverKept) coverHeight = cardFileCover;
  freeCoverBuffer();
#ifdef TENOR_PRESS_PROBE
  const uint32_t fileMs = millis();
#endif
  // CJK titles stay on the body font: its SD fallback covers them, the title font has none.
  const int titleFont = homeExcerptUsesUiFont(title.c_str()) ? UI_12_FONT_ID : UI_TITLE_FONT_ID;
  const auto titleLines = renderer.wrappedText(titleFont, title.c_str(), frame.textW, 2, EpdFontFamily::BOLD);
  in.titleLines = std::max(1, static_cast<int>(titleLines.size()));
  in.titleDrawLineHeight = renderer.getLineHeight(titleFont);
  const auto card = homeCardLayout(in);
  int y = card.titleY;
  for (const auto& line : titleLines) {
    renderer.drawText(titleFont, card.textX, y, line.c_str(), true, EpdFontFamily::BOLD);
    y += in.titleDrawLineHeight;
  }
  renderer.drawText(UI_10_FONT_ID, card.textX, card.authorY,
                    renderer.truncatedText(UI_10_FONT_ID, author, card.textW).c_str());
  const bool cjkExcerpt = homeExcerptUsesUiFont(excerpt.c_str());
  const int quoteFont = excerpt.empty() ? SMALL_FONT_ID : cjkExcerpt ? UI_12_FONT_ID : serifFont;
  const auto quoteStyle = excerpt.empty() || cjkExcerpt ? EpdFontFamily::REGULAR : EpdFontFamily::ITALIC;
  // Without a page slot every glyph miss inflates a whole font group again
  // (the decompressor keeps one hot group), which made this card cost ~620 ms
  // on the X3. Prewarm once; the slot is released after the card is cached.
  auto* fcm = renderer.getFontCacheManager();
  if (fcm && card.excerptLines > 0) fcm->prewarmCache(quoteFont, quote.c_str(), static_cast<uint8_t>(1u << quoteStyle));
  // A two-line title leaves no room for the excerpt.
  y = card.excerptLines > 0 ? card.excerptY : card.authorY + in.authorLineHeight;
  for (const auto& line : renderer.wrappedText(quoteFont, quote.c_str(), card.textW, card.excerptLines, quoteStyle)) {
    renderer.drawText(quoteFont, card.textX, y, line.c_str(), true, quoteStyle);
    y += renderer.getLineHeight(quoteFont);
  }
  const int textBottom = std::min(y, card.ruleY - 1);
#ifdef TENOR_PRESS_PROBE
  const uint32_t textMs = millis();
#endif
  // The reader writes a thumbnail at the card's own height and one at the theme's; the card's is
  // drawn at its own size, the theme's is the fallback for a book not opened since that began.
  bool image = coverKept;
  if (!coverKept) cardFileThumb = 0;
  for (const int height : {HOME_CARD_COVER_H, UITheme::getInstance().getMetrics().homeCoverHeight}) {
    if (image || book.coverBmpPath.empty()) break;
    HalFile file;
    if (!Storage.openFileForRead("HOME", UITheme::getCoverThumbPath(book.coverBmpPath, height), file)) continue;
    if (height == HOME_CARD_COVER_H) cardFileThumb = 1;
    Bitmap bitmap(file);
#ifdef TENOR_PRESS_PROBE
    LOG_INF("HOME", "CARD_COVER_START height=%d free=%u largest=%u", height, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    const uint32_t coverStarted = millis();
#endif
    if (bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() > 0 && bitmap.getHeight() > 0)
      image = renderer.drawBitmapCover(bitmap, card.coverX, card.coverY, card.coverW, card.coverH);
#ifdef TENOR_PRESS_PROBE
    LOG_INF("HOME", "CARD_COVER_END height=%d ms=%lu ok=%u free=%u largest=%u", height,
            static_cast<unsigned long>(millis() - coverStarted), image ? 1u : 0u,
            ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#endif
    if (image) coverHeight = height;
  }
  if (!image) {
    // The brand placeholder, scaled up. The source column of each cover column is worked out once (the
    // C3 has no divider worth a division per pixel: 134.100 of them took 174 ms), and the page is
    // already white, so only the dark pixels are drawn.
    uint8_t column[HOME_CARD_COVER_W];
    const int columns = std::min(card.coverW, HOME_CARD_COVER_W);
    for (int xx = 0; xx < columns; ++xx) column[xx] = static_cast<uint8_t>(xx * sleepcover::WIDTH / card.coverW);
    for (int yy = 0; yy < card.coverH; ++yy) {
      const uint8_t* row = sleepcover::PIXELS + (yy * sleepcover::HEIGHT / card.coverH) * (sleepcover::WIDTH / 8);
      for (int xx = 0; xx < columns; ++xx)
        if (!((row[column[xx] >> 3] >> (7 - (column[xx] & 7))) & 1))
          renderer.drawPixel(card.coverX + xx, card.coverY + yy, true);
    }
  }
  // Round the cover's corners by painting the page back over them, the pixels a rounded card
  // would not cover: a few hundred pixels, no second buffer, and the cached card keeps them.
  if (!coverKept)
    renderer.maskRoundedRectOutsideCorners(card.coverX, card.coverY, card.coverW, card.coverH,
                                           tenorradius::cover(card.coverW));
#ifdef TENOR_PRESS_PROBE
  const uint32_t imageMs = millis();
#endif
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
#ifdef TENOR_PRESS_PROBE
  LOG_INF("HOME", "CARD_STORE bytes=%u ok=%u free=%u largest=%u", static_cast<unsigned>(coverBufferSize),
          coverBufferStored ? 1u : 0u, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#endif
  coverBufferBook = shown;
  coverRendered = true;
  if (coverBufferStored && !cardPath.empty()) {
    cardFilePending = cardPath;
    cardFileCoverKey = coverKey;
    cardFileKey = key;
    cardFileCover = static_cast<int16_t>(coverHeight);
  }
  if (!cardFileThumb && !thumbPath.empty()) wantThumb(shown);
  if (fcm) fcm->releaseBuiltinPageCaches();  // the card is now a cached bitmap
  const int barY = drawCardStats(card);
  drawOtherBookRow(shown, card.ruleY, card.rowY);
#ifdef TENOR_UI_ACCEPTANCE
  LOG_INF("HOME_PROBE", "card_build_us=%lu cache_bytes=%u", static_cast<unsigned long>(micros() - cardStartedUs),
          static_cast<unsigned>(coverBufferSize));
#endif
#ifdef TENOR_PRESS_PROBE
  LOG_INF("HOME", "Card stages quote=%lu file=%lu text=%lu image=%lu rest=%lu", quotedMs - started,
          fileMs - quotedMs, textMs - fileMs, imageMs - textMs, millis() - imageMs);
#endif
  LOG_INF("HOME", "Recent card build=%lums cache=%u cover=%d kept=%u", static_cast<unsigned long>(millis() - started),
          static_cast<unsigned>(coverBufferSize), coverHeight, coverKept ? 1u : 0u);
  LOG_INF("HOME", "Card stats rows=%02x bar=%d", static_cast<unsigned>(cardStats.rows), barY);
}

HomeActivity::CardFile HomeActivity::loadCardFile(const std::string& path, const uint32_t coverKey,
                                                  const uint32_t key, const std::string& thumbPath) {
  // A failed metadata restore can still leave a readable backup.
  freeink::recoverFile(Storage, path.c_str());
  const std::string backup = path + ".davbak";
  const auto load = [&](const char* candidate) -> CardFile {
    auto file = Storage.open(candidate);
    if (!file) return CardFile::None;
    CardFileHead head;
    if (file.read(&head, sizeof(head)) != static_cast<int>(sizeof(head)) || head.magic != CARD_FILE_MAGIC ||
        head.coverKey != coverKey || file.size() != sizeof(head) + head.bytes)
      return CardFile::None;
    if (Storage.exists(thumbPath.c_str()) != static_cast<bool>(head.thumb)) return CardFile::None;
    const auto* r = head.rects;
    const size_t first = renderer.getRegionByteSize(r[0], r[1], r[2], r[3]);
    if (first == 0 || first + (r[7] > 0 ? renderer.getRegionByteSize(r[4], r[5], r[6], r[7]) : 0) != head.bytes)
      return CardFile::None;
    // The cover alone is read when the text is stale.
    const bool whole = head.key == key;
    const size_t bytes = whole ? head.bytes : first;
    freeCoverBuffer();
    coverBuffer = static_cast<uint8_t*>(malloc(bytes));
    if (!coverBuffer) return CardFile::None;
    if (file.read(coverBuffer, bytes) != static_cast<int>(bytes)) {
      freeCoverBuffer();
      return CardFile::None;
    }
    if (!file.close()) {
      freeCoverBuffer();
      return CardFile::None;
    }
    cardFileThumb = head.thumb;
    cardFileCover = head.cover;
    coverBufferSize = bytes;
    coverBufferUiSize = normalizedUiTextSize(SETTINGS.uiTextSize);
    coverRectX = r[0];
    coverRectY = r[1];
    coverRectW = r[2];
    coverRectH = r[3];
    textRectX = r[4];
    textRectY = r[5];
    textRectW = r[6];
    textRectH = whole ? r[7] : 0;
    coverBufferStored = true;
    cardOnCard = whole;
    return whole ? CardFile::Whole : CardFile::Cover;
  };
  const CardFile main = load(path.c_str());
  if (main != CardFile::None) return main;
  // A failed read/allocation does not prove main corrupt. Serve a validated
  // backup without destroying either copy; replacement remains blocked.
  return load(backup.c_str());
}

void HomeActivity::wantThumb(const int index) {
  if (!FsHelpers::hasEpubExtension(recentBooks[index].path)) return;
  const uint32_t id = fnv(2166136261u, recentBooks[index].path);
  for (const uint32_t tried : thumbTried)
    if (tried == id) return;
  thumbWantedAtMs = millis();
  thumbWanted = static_cast<int8_t>(index);
}

void HomeActivity::onTick() {
  if (wakeStatePending.exchange(false)) saveAppState();
  const int index = thumbWanted.load();
  if (index < 0) return;
  // Any press moves the card on or opens something: the decode waits for the next idle card.
  if (mappedInput.wasAnyPressed() || mappedInput.wasAnyReleased()) {
    thumbWanted = -1;
    return;
  }
  if (millis() - thumbWantedAtMs.load() < CARD_THUMB_IDLE_MS) return;
  thumbWanted = -1;
  writeMissingThumb(index);
}

void HomeActivity::writeMissingThumb(const int index) {
  RenderLock lock(*this);
  if (activeTabId != Tab::RECENT || index != shownRecent()) return;
  const std::string path = recentBooks[index].path;
  thumbTried[thumbTriedNext++ % 5] = fnv(2166136261u, path);
  // The card is drawn again with its cover, and the decode gets the card's snapshot memory.
  freeCoverBuffer();
  coverRendered = false;
  Epub epub(path, "/.crosspoint");
  // Where the reader left the cover's path: the book's index is not loaded to find it.
  const unsigned long refStarted = millis();
  std::string href;
  const bool ref = coverref::load(epub.getCachePath(), href);
  const size_t heap = ESP.getFreeHeap();
  LOG_PROBE("HOME", "Card cover ref ok=%u ms=%lu free=%u", ref ? 1u : 0u, millis() - refStarted,
          static_cast<unsigned>(heap));
  if (heap >= (ref ? CARD_THUMB_REF_MIN_FREE_HEAP : CARD_THUMB_MIN_FREE_HEAP) &&
      ESP.getMaxAllocHeap() >= CARD_THUMB_MIN_LARGEST_BLOCK) {
    LOG_INF("HOME", "Card thumbnail write %d", index);
    GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
    const int heights[] = {HOME_CARD_COVER_H, UITheme::getInstance().getMetrics().homeCoverHeight};
    if (ref) {
      epub.generateThumbBmps(href, heights, 2);
    } else if (epub.load(false, true)) {
      epub.generateThumbBmps(heights, 2);
    }
  }
  requestUpdate();
}

// Runs after the frame is on the panel. Checked staging preserves the committed card
// until the full header and payload have synced and closed.
void HomeActivity::saveCardFile() {
  const std::string path = std::move(cardFilePending);
  cardFilePending.clear();
  if (!coverBufferStored || !coverBuffer) return;
#ifdef TENOR_PRESS_PROBE
  const uint32_t started = millis();
#endif
  const CardFileHead head{CARD_FILE_MAGIC,
                          cardFileCoverKey,
                          cardFileKey,
                          {static_cast<int16_t>(coverRectX), static_cast<int16_t>(coverRectY),
                           static_cast<int16_t>(coverRectW), static_cast<int16_t>(coverRectH),
                           static_cast<int16_t>(textRectX), static_cast<int16_t>(textRectY),
                           static_cast<int16_t>(textRectW), static_cast<int16_t>(textRectH)},
                          static_cast<uint32_t>(coverBufferSize),
                          cardFileThumb,
                          cardFileCover};
  cardOnCard = false;
  if (!freeink::recoverFile(Storage, path.c_str())) return;
  const std::string staging = path + ".davtmp";
  const std::string backup = path + ".davbak";
  if (Storage.exists(backup.c_str())) {
    // Cache identity can be stale while the committed file remains complete.
    // Distinguish corrupt metadata from unavailable I/O before retiring backup.
    const auto validate = [&](const char* candidate) {
      if (!Storage.exists(candidate)) return 0;
      auto probe = Storage.open(candidate);
      if (!probe) return -1;
      CardFileHead saved{};
      const size_t size = probe.size();
      if (size < sizeof(saved)) return probe.close() ? 0 : -1;
      const bool read = probe.read(&saved, sizeof(saved)) == static_cast<int>(sizeof(saved));
      if (!probe.close() || !read) return -1;
      const auto* r = saved.rects;
      const size_t first = renderer.getRegionByteSize(r[0], r[1], r[2], r[3]);
      return saved.magic == CARD_FILE_MAGIC && size == sizeof(saved) + saved.bytes && first > 0 &&
                     first + (r[7] > 0 ? renderer.getRegionByteSize(r[4], r[5], r[6], r[7]) : 0) == saved.bytes
                 ? 1 : 0;
    };
    const int main = validate(path.c_str());
    if (main < 0) return;
    if (main == 1) {
      if (!Storage.remove(backup.c_str())) return;
    } else {
      if (validate(backup.c_str()) != 1) return;
      if (Storage.exists(path.c_str()) && !Storage.remove(path.c_str())) return;
      if (!freeink::recoverFile(Storage, path.c_str())) return;
    }
  }
  auto file = Storage.open(staging.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
  const bool written = file && file.write(&head, sizeof(head)) == sizeof(head) &&
                       file.write(coverBuffer, coverBufferSize) == coverBufferSize;
  const bool synced = written && file.sync();
  const bool closed = file.close();
  bool valid = false;
  if (synced && closed) {
    auto check = Storage.open(staging.c_str());
    CardFileHead saved{};
    valid = check && check.read(&saved, sizeof(saved)) == static_cast<int>(sizeof(saved)) &&
            saved.magic == CARD_FILE_MAGIC && saved.coverKey == head.coverKey && saved.key == head.key &&
            saved.bytes == head.bytes && saved.thumb == head.thumb && saved.cover == head.cover &&
            std::memcmp(saved.rects, head.rects, sizeof(head.rects)) == 0 &&
            check.size() == sizeof(saved) + saved.bytes;
    valid = check.close() && valid;
  }
  const bool ok = valid && freeink::replaceFile(Storage, staging.c_str(), path.c_str());
  if (!ok) Storage.remove(staging.c_str());
  cardOnCard = ok;
#ifdef TENOR_PRESS_PROBE
  LOG_INF("HOME", "Card file saved ok=%u ms=%lu", ok ? 1u : 0u, static_cast<unsigned long>(millis() - started));
#endif
}

// "Another book" and the next book's title under a rule, with an open V on each side that has a
// book to step to. Drawn on every paint: it is one line of an uncompressed flash font. The arrows
// sit outside the text margins as mirror images, so the title ends on the right margin as the
// label starts on the left one.
void HomeActivity::drawOtherBookRow(const int shown, const int ruleY, const int rowY) {
  const int count = static_cast<int>(recentBooks.size());
  if (count < 2) return;
  // The arrows' tips stay where the solid triangles had them, 24 px outside the text margins.
  constexpr int MARGIN = 40, ARROW_SPAN = 7, ARROW_OUT = 24;
  const int left = MARGIN, right = renderer.getScreenWidth() - MARGIN;
  renderer.drawLine(left, ruleY, right - 1, ruleY);
  const char* label = tr(STR_RECENT_OTHER_BOOK);
  renderer.drawText(UI_10_FONT_ID, left, rowY, label);
  const auto& next = recentBooks[(shown + 1) % count];
  const auto title = next.title.empty() ? next.path.substr(next.path.find_last_of('/') + 1) : next.title;
  const int room = right - left - renderer.getTextWidth(UI_10_FONT_ID, label) - 16;
  const auto shownTitle = renderer.truncatedText(UI_10_FONT_ID, title.c_str(), room, EpdFontFamily::BOLD);
  const int titleWidth = renderer.getTextWidth(UI_10_FONT_ID, shownTitle.c_str(), EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, right - titleWidth, rowY, shownTitle.c_str(), true, EpdFontFamily::BOLD);
  const int top = rowY + renderer.getFontAscenderSize(UI_10_FONT_ID) * 2 / 3 - ARROW_SPAN;
  const int length = tenorchrome::moreChevronLength(ARROW_SPAN);
  tenorchrome::drawMoreChevron(renderer, right + ARROW_OUT - length, top, tenorchrome::ChevronDir::Right, ARROW_SPAN);
  // Right wraps to the most recent book from the last one, so only the left arrow can be missing.
  if (shown > 0)
    tenorchrome::drawMoreChevron(renderer, left - ARROW_OUT, top, tenorchrome::ChevronDir::Left, ARROW_SPAN);
}
