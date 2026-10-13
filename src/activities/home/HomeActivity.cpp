#include "HomeActivity.h"

#include <FontCacheManager.h>

#include <Bitmap.h>
#include <CoverInk.h>
#include <Epub.h>
#include <Epub/converters/ImageToFramebufferDecoder.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <HeapFloor.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <RecoverableFile.h>
#include <Utf8.h>
#include <Xtc.h>

#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <functional>
#include <vector>

#include "BookStatsActivity.h"
#include "BookStatsLibraryActivity.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "DocThuMuc.h"
#include "FileActions.h"
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
#include "FileBrowserState.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "activities/util/ListOrderActivity.h"
#include "components/ReadingStatsFormat.h"
#include "components/ReadingStatsView.h"
#include "util/CoverRef.h"
#include "util/NgayDocXong.h"
#include "components/HomeStatsNavigation.h"
#include "components/CompactStats.h"
#include "components/CoverHolder.h"
#include "components/HomeExcerptStyle.h"
#include "components/HomeCoverCorners.h"
#include <TenorInk.h>
#include "components/SettledListRender.h"
#include "components/TenorMenuChrome.h"
#include "HeapBigProbe.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "components/themes/TenorRadius.h"
#include "components/ContextMenuBackground.h"
#include "components/icons/tenorHomeTabIcons.h"
#include "fontIds.h"

namespace fui = freeink::ui;
#if defined(FREEINK_DEVICE_X3) && FREEINK_DEVICE_X3
static constexpr bool kInkX3 = true;
#else
static constexpr bool kInkX3 = false;
#endif
// X3 Home draws tenor/ink 1 bit. Its 4-level pass decodes the cover once per strip and plane
// (14 decodes, 5.7 s measured on an X3 with input held); turn it on once one decode feeds every strip.
static constexpr bool kInkX3Gray = false;

void saveAppState();  // main.cpp

// The hour's budget of idle work outlives deep sleep in RTC memory, read with the RTC clock that
// runs through it (a wake restarts the chip). A power loss leaves it unmarked: a new hour.
#if defined(SIMULATOR)
static uint64_t idleClockUs() { return static_cast<uint64_t>(millis()) * 1000; }
#else
extern "C" uint64_t esp_rtc_get_time_us(void);
static uint64_t idleClockUs() { return esp_rtc_get_time_us(); }
#endif
RTC_NOINIT_ATTR static idlework::Budget idleBudget;

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
// "CRD6": a book without a cover keeps the placeholder (white, the book's name and author) on its card. A
// card an older firmware wrote under the same version string holds a former placeholder, the dithered
// picture of components/CoverHolder.h, and is drawn again; its file is replaced at the same name.
constexpr uint32_t CARD_FILE_MAGIC = 0x36445243;

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
// Idle time before Home writes the cover placeholder of a frame size it has none for.
constexpr uint32_t HOLDER_IDLE_MS = 1000;
// Frame sizes Home has tried to write a placeholder for since boot, as sized and as levels, so a
// card that cannot be written is not tried on every visit.
uint32_t holderTried[8] = {};
constexpr uint32_t holderId(const int width, const int height, const bool four) {
  return (static_cast<uint32_t>(width) << 17) | (static_cast<uint32_t>(height) << 1) | (four ? 1u : 0u);
}
uint8_t holderTriedNext = 0;
}  // namespace

HomeActivity::HomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                           const HomeMenuItem initialMenuItemValue, const bool cleanInitialRefresh)
    : UiTabListActivity("Home", renderer, mappedInput, tenorchrome::kTouchShell),
      initialMenuItem(initialMenuItemValue),
      cleanInitialRefresh(cleanInitialRefresh),
      leftBook(activityManager.isReaderActivity()) {}

// The one rule for driving the cover again: the first frame after a book, on the Recent card, when
// that frame was not already a full refresh. The page's gray levels stay on the glass wherever its
// black and white matches the cover's (the X3 UC8279 drives only pixels that change), and the
// cover's halftone shows them as the old page. A later frame of the same Home is not due.
bool HomeActivity::coverRedriveDue() {
  const bool due = leftBook && !cleanInitialRefresh && activeTabId == Tab::RECENT && coverRectW > 0 && coverRectH > 0;
  leftBook = false;
  return due;
}

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
  restoringNavigation = true;  // coming back onto a card is no choice of it
  UiTabListActivity::restoreNavigation(restored);
  restoringNavigation = false;
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
  stopFolderRecursiveSearch();
  handOffHomeGhost();
  homeRefresh.leave();
  clearHomeGray();
  freeCoverBuffer();
  UiTabListActivity::onExit();
}

// Back in front: a cover whose step ended this visit (a failed read, a copy) is tried again.
void HomeActivity::onResume() {
  UiTabListActivity::onResume();
  idleQueue.clear(true);
}

void HomeActivity::onPause() {
  handOffHomeGhost();
  homeRefresh.leave();
  // The manager holds RenderLock while pausing an activity.
  clearHomeGray();
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

// The last folder of File in tenor/cross (founder 12/10/2026), kept across sleep; "/" when there is none, it
// is the root, or the card no longer has it.
std::string HomeActivity::lastFolder() const {
  if (shell::isUgly()) return "/";
  return filebrowser::lastFolder(false, [](const char* p) {
    auto dir = Storage.open(p);
    return static_cast<bool>(dir) && dir.isDirectory();
  });
}

// A tap on the File card from another card opens the last folder. A step through the cards (buttons, tilt)
// passes the card by: its first row holds the last folder instead (docGocTheNho).
bool HomeActivity::openLastFolder() {
  const std::string path = lastFolder();
  if (path == "/") return false;
  selectTab(Tab::FOLDER);  // File is the zone of the bar there, and its ‹ comes back to this card
  activityManager.goToFileBrowser(path);
  return true;
}

void HomeActivity::stepTab(const int direction) {
  const int next = adjacentTab(direction);
  selectTab(static_cast<Tab>(next));
  requestUpdate();
}

void HomeActivity::onTabAction(const int index) {
  if (tenorchrome::kTouchShell && !restoringNavigation && static_cast<Tab>(index) == Tab::FOLDER &&
      activeTabId != Tab::FOLDER && openLastFolder()) {
    app.clearTapFlash();
    return;
  }
  selectTab(static_cast<Tab>(index));
  app.clearTapFlash();
}

// Rows of the active tab. Structural: call it when the tab changes or the
// recent list reloads, never from buildScreen().
void HomeActivity::rebuildRows(const bool readFolder) {
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
    case Tab::FOLDER: {
      if (readFolder && !folderRecursiveSearch) docGocTheNho();
      if (folderRecursiveSearch) break;
      if (tenorchrome::kTouchShell &&
          !filesearch::buildResults(mucTheNho, folderSearchQuery, folderRowIndices, "home-file-search-results")) {
        LOG_ERR("Home", "Search result allocation refused");
        folderSearchIncomplete = !folderSearchQuery.empty();
      }
      const size_t rowCount = tenorchrome::kTouchShell ? folderRowIndices.size() : mucTheNho.size();
      for (size_t row = 0; row < rowCount; ++row) {
        const size_t fileIndex = tenorchrome::kTouchShell ? folderRowIndices[row] : row;
        const auto& muc = mucTheNho[fileIndex];
        const bool last = lastFolderRow && fileIndex == 0;
        if (last) {
          const std::string path = muc.substr(0, muc.size() - 1);
          char line[160];
          snprintf(line, sizeof(line), tr(STR_FILE_REOPEN), path.substr(path.find_last_of('/') + 1).c_str());
          rowLabels.emplace_back(line);
        } else if (tenorchrome::kTouchShell) {
          char name[128];
          formatFileName(muc, name, sizeof(name));
          rowLabels.emplace_back(name);
        } else rowLabels.push_back(muc);
      }
#ifdef TENOR_PRESS_PROBE
      LOG_INF("FILES_SEARCH", "source_n=%u result_n=%u alloc=%u free=%u largest=%u",
              unsigned(mucTheNho.size()), unsigned(rowCount), unsigned(folderRowIndices.capacity() * sizeof(uint16_t)),
              unsigned(ESP.getFreeHeap()), unsigned(ESP.getMaxAllocHeap()));
#endif
      break;
    }
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
      item.icon = listIconFor(UITheme::getFileIcon(folderEntry(static_cast<int>(i))), 32);
    if (lastFolderAt(static_cast<int>(i))) item.icon = freeink::ui::bitmapFromIcon(icon_recent_32);
    item.actionValue = static_cast<int16_t>(i);
    rowItems.push_back(item);
  }
}

void HomeActivity::activateIndex(const int index) {
  commitTabNavigation();
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();

  switch (activeTabId) {
    case Tab::RECENT:
      onSelectBook(recentBooks[index].path);
      return;
    case Tab::FOLDER: {
      const std::string& ten = folderEntry(index);
      if (!ten.empty() && ten.back() == '/') {
        auto path = folderFullPath(index);
        path.pop_back();
        activityManager.goToFileBrowser(path);
      } else {
        onSelectBook(folderFullPath(index));
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

bool HomeActivity::handleCustomInput() {
  if (!folderSearchScanner || (folderSearchScanner->reason() != filesearch::ScanReason::Running &&
                               folderSearchScanner->reason() != filesearch::ScanReason::Cancelled)) return false;
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    RenderLock lock(*this);
    stopFolderRecursiveSearch();
    folderSearchQuery.clear();
    folderRecursiveSearch = false;
    folderSearchIncomplete = false;
    rebuildRows(false);
    activeNav().reset();
    requestUpdate();
    return true;
  }
  tickFolderRecursiveSearch();
  return true;
}

bool HomeActivity::handleButtons() {
  if (pendingRecentRemoval) {
    RecentBook book = std::move(*pendingRecentRemoval);
    pendingRecentRemoval.reset();
    showRecentRemovalConfirmation(std::move(book));
    return true;
  }
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
    if (activeTabId == Tab::FOLDER && index >= 0 && index < listCount() && folderEntry(index).back() != '/')
      path = folderFullPath(index);
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
  const std::string statsQuestion =
      I18N.get(all ? StrId::STR_ALERT_STATS_ALL_QUESTION : StrId::STR_ALERT_STATS_HABITS_QUESTION);
  std::string statsLongAction = I18N.get(all ? StrId::STR_STATS_RESET_ALL : StrId::STR_STATS_RESET_HABITS);
  if (!statsLongAction.empty() && statsLongAction.back() == '?') statsLongAction.pop_back();
  startActivityForResult(
      makeUniqueNoThrow<ConfirmationActivity>(
          renderer, mappedInput, I18N.get(all ? StrId::STR_STATS_RESET_ALL : StrId::STR_STATS_RESET_HABITS),
          I18N.get(all ? StrId::STR_STATS_RESET_ALL_BODY : StrId::STR_STATS_RESET_HABITS_BODY),
          ConfirmationContent{statsQuestion,
                              I18N.get(all ? StrId::STR_STATS_RESET_ALL_BODY : StrId::STR_STATS_RESET_HABITS_BODY),
                              I18N.get(all ? StrId::STR_ALERT_STATS_ACTION_SHORT : StrId::STR_ALERT_STATS_HABITS_ACTION_SHORT),
                              statsLongAction, "", true}),
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
  if (tenorchrome::x3ScreenLayoutEnabled()) {
    const auto layout = homeScreenLayout();
    if (const char* tip = homeTipText())
      tenorchrome::drawTipInRect(renderer, tip, layout.tipRect, 6);
    if (const char* info = homeInfoText()) {
      const int height = tenorchrome::tipHeight(renderer, info, 3);
      tenorchrome::drawInfoInRect(renderer, info,
                                {24, layout.bodyRect.bottom() - height, renderer.getScreenWidth() - 48, height}, 3);
    }
    return;
  }
  if (activeTabId == Tab::FOLDER && ringPos() == 0) tenorchrome::drawTip(renderer, tr(STR_FOLDER_HOLD));
  if (favoriteFileMissing) tenorchrome::drawTip(renderer, missingFileText(), 1, 3);
  if (activeTabId == Tab::STATS && statsResetTip) tenorchrome::drawTip(renderer, I18N.get(*statsResetTip), 1, 2);
}

void HomeActivity::drawChrome() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();

  if (!tenorchrome::x3ScreenLayoutEnabled()) {
    if (tenorchrome::enabled())
      drawNavigationHeader(tabLabel(activeTab()));
    else
      GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding - metrics.topPadding},
                     tabLabel(activeTab()), nullptr, false);
  }

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
  if (tenorchrome::x3ScreenLayoutEnabled()) return tenorchrome::x3ScreenLayout(renderer).bodyRect.y;
  const auto& metrics = UITheme::getInstance().getMetrics();
  if (!tenorchrome::enabled())
    return metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight;
  return tabBarTop() + (tenorchrome::kTouchShell ? 0 : preferredTabBarHeight()) + 16;
}

const char* HomeActivity::homeTipText() {
  if (activeTabId == Tab::FOLDER && ringPos() == 0) return tr(STR_FOLDER_HOLD);
  if (activeTabId == Tab::FAVORITES && rowItems.empty()) return tr(STR_HOME_FAVORITES_HINT);
  if (!supportsFavorites() || favoriteSaveFailed) return nullptr;
  if (activeTabId == Tab::FOLDER && ringPos() > 0) return tr(STR_FILE_HOLD_ACTIONS);
  return UiListActivity::favoriteHintText();
}

// A pin whose book or folder is gone: the touch shell has no Select to name, and a line naming one would be hidden.
const char* HomeActivity::missingFileText() {
  return tenorchrome::kTouchShell ? tr(STR_FILE_NOT_FOUND_TOUCH) : tr(STR_FILE_NOT_FOUND);
}

const char* HomeActivity::homeInfoText() {
  if (favoriteFileMissing) return missingFileText();
  if (activeTabId == Tab::STATS && statsResetTip) return I18N.get(*statsResetTip);
  if (recentRemovalFailed) return tr(STR_REMOVE_FROM_RECENTS_FAILED);
  if (favoriteSaveFailed) return tr(STR_MENU_SAVE_FAILED);
  return nullptr;
}

tenorchrome::X3ScreenLayout HomeActivity::homeScreenLayout() {
  const char* tip = homeTipText();
  return tenorchrome::x3ScreenLayout(renderer, tip && tenorchrome::tipShown(tip)
                                                ? tenorchrome::tipHeight(renderer, tip, 6) : 0);
}

const char* HomeActivity::favoriteHintText() {
  return tenorchrome::x3ScreenLayoutEnabled() ? nullptr : UiListActivity::favoriteHintText();
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

  if (tenorchrome::x3ScreenLayoutEnabled()) {
    const auto layout = homeScreenLayout();
    screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(layout.bodyRect.y), 0,
        static_cast<int16_t>(renderer.getScreenHeight() - layout.bodyRect.bottom()), 0});
    if (const char* info = homeInfoText())
      screen.takeBottom(static_cast<int16_t>(8 + tenorchrome::tipHeight(renderer, info, 3)));
  }

  buildTabBar(screen);
  buildFileSearch(screen);
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
      const auto layout = recentLayout();
      // A hold pins or unpins the book shown, like holding Select on the button readers.
      screen.frame().hit(body, ACTION_ROW, static_cast<int16_t>(shownRecent()), fui::InputTouch | fui::InputLongPress);
      // The "other books" line steps to the next book instead of opening the one shown.
      if (layout.style == HomeRecentStyle::Shelf) {
        for (int slot = 0; slot < layout.shelfCount; ++slot) {
          const auto& r = layout.shelf[slot];
          screen.frame().hit({static_cast<int16_t>(24 + slot * 152), static_cast<int16_t>(r.y), 128,
                              static_cast<int16_t>(r.h + layout.shelfLabelHeight)},
                             ACTION_ROW, static_cast<int16_t>(layout.shelfBooks[slot]),
                             fui::InputTouch | fui::InputLongPress);
        }
      } else if (count > 1) {
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
    } else if (tenorchrome::x3ScreenLayoutEnabled())
      screen.centeredText(tr(STR_FAVORITES_EMPTY), screen.theme().bodyText);
    else
      tenorchrome::drawTip(renderer, tr(STR_HOME_FAVORITES_HINT), 0, 6);
    return;
  }
  if (activeTabId == Tab::FOLDER && folderSearchIncomplete) {
    const auto band = screen.takeTop(static_cast<int16_t>(renderer.getLineHeight(SMALL_FONT_ID) + 8), 4);
    fui::TextStyle status = screen.theme().smallText;
    status.align = fui::TextAlign::Center;
    screen.target().text(band, tr(STR_SEARCH_INCOMPLETE), status);
  }
  if (activeTabId == Tab::FOLDER && listCount() == 0) {
    screen.centeredText(mucQuaNhieu ? tr(STR_FOLDER_TOO_MANY) : tr(STR_NO_FILES_FOUND), screen.theme().bodyText);
    return;
  }

  if (!tenorchrome::x3ScreenLayoutEnabled() && favoriteFileMissing)
    screen.takeBottom(static_cast<int16_t>(28 + tenorchrome::tipHeight(renderer, missingFileText(), 3)));
  if (!tenorchrome::x3ScreenLayoutEnabled() && activeTabId == Tab::STATS && statsResetTip)
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
  if (activeTabId == Tab::FOLDER && folderRecursiveSearch) {
    props.items = nullptr;
    props.rowProvider = &HomeActivity::searchRow;
    props.rowProviderCtx = this;
  }
  props.count = static_cast<uint16_t>(listCount());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  if (tenorchrome::kTouchShell) props.inputMask |= fui::InputLongPress;  // hold a row to pin it
  props.labelText = uiMenuLabelText(screen.theme());
  props.labelText.maxLines = 2;
  if (activeTabId == Tab::FOLDER && folderRecursiveSearch) {
    props.subtitleText = screen.theme().smallText;
    props.subtitleText.maxLines = 1;
  }
  syncTabListViewport(screen, props, activeTabId == Tab::FOLDER && folderRecursiveSearch);
  screen.list(props);
}

HomeRecentRect HomeActivity::refreshBody() {
  if (tenorchrome::x3ScreenLayoutEnabled()) {
    const auto body = homeScreenLayout().bodyRect;
    return {0, body.y, renderer.getScreenWidth(), body.height};
  }
  const int top = coverTileTop() - 4;
  return {0, top, renderer.getScreenWidth(), tenorchrome::touchBarTop(renderer.getScreenHeight()) - 8 - top};
}

void HomeActivity::render(RenderLock&&) {
  if (activeTabId == Tab::FOLDER && folderSearchScanner &&
      folderSearchScanner->reason() == filesearch::ScanReason::Running) return;
  if (rowMenu.isActive() && activeTabId == Tab::RECENT && !shell::isUgly()) {
    if (!tenorchrome::kTouchShell && tenorchrome::x3ScreenLayoutEnabled()) {
      const auto tip = homeScreenLayout().tipRect;
      renderer.fillRect(tip.x, tip.y, tip.width, tip.height, false);
    }
  }
  if (rowMenu.processRender(renderer, mappedInput)) {
    homeRefresh.leave();
    return;
  }
  if (activeTabId == Tab::STATS && statsRowsEnlarged != statsPaged()) rebuildRows();
#ifdef TENOR_UI_ACCEPTANCE
  const uint32_t totalStartedUs = micros();
  const uint32_t paintStartedUs = totalStartedUs;
#endif
  const uint32_t started = millis();
  const bool recent = activeTabId == Tab::RECENT && homeInkEnabled();
  const uint16_t surface = (static_cast<uint16_t>(renderer.getOrientation()) << 8) |
                           (normalizedUiTextSize(SETTINGS.uiTextSize) << 4) | SETTINGS.homeStyle;
  const auto refresh = homeRefresh.choose(recent, shownRecent(), surface);
  const bool window = refresh.mode == HomeRefresh::Mode::Window;
  const auto body = refreshBody();
  const auto clip = renderer.getClipRect();
  if (window) renderer.setClipRect(body.x, body.y, body.w, body.h);
  const auto clearFrame = [&] {
    if (window) renderer.fillRect(body.x, body.y, body.w, body.h, false);
    else renderer.clearScreen();
  };
  clearHomeGray();
  inkSlotCount = 0;
  const bool nextGray = refresh.oneBit ? false : homeGrayWanted();
  if (homeGraySelected != nextGray) {
    freeCoverBuffer();
    coverRendered = false;
    touchThumbCount = 0;
  }
  homeGraySelected = nextGray;
  clearFrame();
  drawChrome();
#ifdef TENOR_UI_ACCEPTANCE
  const uint32_t chromeUs = micros() - paintStartedUs;
  const uint32_t uiStartedUs = micros();
#endif
  unsigned paintPass = 0;
  renderSettledList(activeNav(), [&] {
    if (paintPass++ > 0) {
      clearFrame();
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
  renderer.setClipRect(clip[0], clip[1], clip[2], clip[3]);
#ifdef TENOR_UI_ACCEPTANCE
  const uint32_t footerUs = micros() - footerStartedUs;
  const uint32_t paintUs = micros() - paintStartedUs;
  const uint32_t displayStartedUs = micros();
#endif
  if (!window && coverRedriveDue()) {
    renderer.redriveNextRefresh(HalDisplay::FAST_REFRESH);
  }
  const bool grayRequested = homeGraySelected;
  if (window) {
    renderer.displayWindow(body.x, body.y, body.w, body.h);
#ifdef TENOR_PRESS_PROBE
    if (refresh.changedBook) {
      LOG_INF("HOME", "HOME_SWAP waves=1 mode=fast rect=%d,%d,%d,%d ms=%lu window=%u",
              kInkX3 ? 0 : body.x, kInkX3 ? 0 : body.y,
              kInkX3 ? renderer.getScreenWidth() : body.w, kInkX3 ? renderer.getScreenHeight() : body.h,
              static_cast<unsigned long>(millis() - started), kInkX3 ? 0u : 1u);
    }
#endif
  } else if (refresh.mode == HomeRefresh::Mode::Restore) {
    restoreHomeGhost("tab");
  } else if (!showHomeGray()) {
    if (grayRequested && !homeGraySelected && kInkX3) {
      homeGraySelected = false;
      freeCoverBuffer();
      coverRendered = false;
      renderer.clearScreen();
      drawChrome();
      renderUi();
      drawFooter();
    }
    renderer.displayBuffer(cleanInitialRefresh ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH);
  }
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
  homeRefresh.painted(recent, shownRecent(), surface, millis());
  queueIdleCovers();
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
  if (tenorchrome::kTouchShell) {
    touchCoverBuffer = HalMemory::allocatePsram(needed);
    coverBuffer = touchCoverBuffer.get();
  } else {
    coverBuffer = static_cast<uint8_t*>(heapfloor::take(needed, "home-cover"));
  }
  if (!coverBuffer) {
    LOG_ERR("HOME", "OOM: cover buffer (%u bytes)", (unsigned)needed);
    return false;
  }
  coverBufferSize = needed;
  coverBufferUiSize = normalizedUiTextSize(SETTINGS.uiTextSize);
  coverBufferStyle = SETTINGS.homeStyle;
  if (!renderer.copyRegionToBuffer(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer, first) ||
      (second > 0 &&
       !renderer.copyRegionToBuffer(textRectX, textRectY, textRectW, textRectH, coverBuffer + first, second))) {
    if (touchCoverBuffer) touchCoverBuffer.reset();
    else free(coverBuffer);
    coverBuffer = nullptr;
    coverBufferSize = 0;
    return false;
  }
  return true;
}

bool HomeActivity::restoreCoverBuffer() {
  if (coverBufferUiSize != normalizedUiTextSize(SETTINGS.uiTextSize) ||
      (tenorchrome::kTouchShell && coverBufferStyle != SETTINGS.homeStyle)) {
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
    if (touchCoverBuffer) touchCoverBuffer.reset();
    else free(coverBuffer);
    coverBuffer = nullptr;
  }
  coverBufferSize = 0;
  coverBufferStored = false;
  coverBufferBook = -1;
  cardOnCard = false;
}

void HomeActivity::loadRecentBooks() {
  recentBooks = homerows::recent(RECENT_LIMIT);
  cardCoverSizesRead = 0;
}

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
  docthumuc::doc("/", SETTINGS.showHiddenFiles, docthumuc::Loc::Sach, dem.get(), DEM_CO, mucTheNho, cap, &mucQuaNhieu,
                 filebrowser::settingsSort());
  // First row: the last folder of File, a folder entry like the others (relative, trailing '/'). Not a pin.
  const std::string last = mucQuaNhieu ? "/" : lastFolder();
  lastFolderRow = last != "/";
  if (lastFolderRow) mucTheNho.insert(mucTheNho.begin(), last.substr(1) + "/");
}

bool HomeActivity::lastFolderAt(const int row) const {
  return activeTabId == Tab::FOLDER && lastFolderRow && row >= 0 && row < static_cast<int>(rowLabels.size()) &&
         &folderEntry(row) == &mucTheNho.front();
}

void HomeActivity::startFolderRecursiveSearch() {
  stopFolderRecursiveSearch();
  folderRecursiveSearch = true;
  folderSearchIncomplete = false;
  folderSearchCancelled = false;
  folderRowIndices.clear();
  folderSearchStorage = makeUniqueNoThrow<filesearch::HalStorageAdapter>();
  folderSearchHooks = makeUniqueNoThrow<filesearch::HalScanHooks>(folderSearchCancelled, SETTINGS.showHiddenFiles, mappedInput);
  folderSearchScanner = makeUniqueNoThrow<filesearch::Scanner>();
  if (!folderSearchStorage || !folderSearchHooks || !folderSearchScanner ||
      !folderSearchScanner->start(*folderSearchStorage, *folderSearchHooks, "/", folderSearchQuery)) {
    LOG_ERR("Home", "Recursive file search refused");
    stopFolderRecursiveSearch();
    folderRecursiveSearch = false;
    folderSearchIncomplete = true;
    rebuildRows(false);
    return;
  }
  rebuildRows(false);
}

void HomeActivity::tickFolderRecursiveSearch() {
  if (!folderSearchScanner || folderSearchScanner->reason() != filesearch::ScanReason::Running) return;
  RenderLock lock(RenderLock::TryTake{});
  if (!lock.acquired()) return;
  const auto reason = folderSearchScanner->tick();
  if (reason == filesearch::ScanReason::Running || reason == filesearch::ScanReason::Cancelled) return;
  folderSearchIncomplete = reason != filesearch::ScanReason::Complete;
  if (activeTabId == Tab::FOLDER) {
    rebuildRows(false);
    activeNav().selected = listCount() == 0 ? 0 : 1;
    activeNav().top = 0;
#ifdef TENOR_PRESS_PROBE
    LOG_INF("FILES_SEARCH", "source_n=%u result_n=%u alloc=%u free=%u largest=%u",
            unsigned(folderSearchScanner->stats().entries), unsigned(folderSearchScanner->resultCount()),
            unsigned(folderSearchScanner->allocatedBytes()), unsigned(ESP.getFreeHeap()), unsigned(ESP.getMaxAllocHeap()));
#endif
    requestUpdate();
  }
}

void HomeActivity::stopFolderRecursiveSearch() {
  if (folderSearchScanner && folderSearchScanner->reason() == filesearch::ScanReason::Running) {
    folderSearchScanner->cancel();
    folderSearchScanner->tick();
  }
  folderSearchScanner.reset();
  folderSearchHooks.reset();
  folderSearchStorage.reset();
  folderSearchCancelled = true;
}

const std::string& HomeActivity::folderEntry(const int row) const {
  if (folderRecursiveSearch && folderSearchScanner) {
    folderSearchEntry = std::string(folderSearchScanner->result(static_cast<size_t>(row)).path);
    return folderSearchEntry;
  }
  return mucTheNho[tenorchrome::kTouchShell ? folderRowIndices[static_cast<size_t>(row)] : static_cast<size_t>(row)];
}

std::string HomeActivity::folderFullPath(const int row) const {
  const auto& entry = folderEntry(row);
  return entry.empty() || entry.front() == '/' ? entry : "/" + entry;
}

void HomeActivity::searchRow(void* ctx, uint16_t index, fui::ListItem& item) {
  auto* self = static_cast<HomeActivity*>(ctx);
  const auto path = self->folderEntry(index);
  formatFileName(std::string(filesearch::basename(path)), self->searchRowName, sizeof(self->searchRowName));
  const auto folder = filesearch::basename(filesearch::parent(path));
  snprintf(self->searchRowFolder, sizeof(self->searchRowFolder), "%.*s", int(folder.size()), folder.data());
  utf8ComposeNfcInPlace(self->searchRowFolder);
  item.label = self->searchRowName;
  if (!folder.empty()) item.subtitle = self->searchRowFolder;
  item.icon = listIconFor(UITheme::getFileIcon(path), 32);
  item.actionValue = static_cast<int16_t>(index);
}

void HomeActivity::openFileSearch() {
  if (!canSearchFiles()) return;
  app.clearTapFlash();
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_SEARCH), folderSearchQuery,
                                                          48, InputType::Text);
  if (!keyboard) return;
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    if (result.isCancelled) return;
    folderSearchQuery = std::get<KeyboardResult>(result.data).text;
    if (folderSearchQuery.empty()) {
      stopFolderRecursiveSearch();
      folderRecursiveSearch = false;
      folderSearchIncomplete = false;
      rebuildRows(false);
    } else {
      startFolderRecursiveSearch();
    }
    activeNav().top = 0;
    activeNav().selected = listCount() == 0 ? 0 : 1;
    requestUpdate();
  });
}

bool HomeActivity::rowStartsGroup(const int row) const {
  return activeTabId == Tab::CAI_DAT && settingsOrder.heading(row) >= 0;
}

bool HomeActivity::rowOpens(const int row) const {
  // Settings groups and the stats screens open a screen; a folder opens its rows; a book opens the book.
  // The enlarged stats tab's page row turns the page in place.
  if (activeTabId == Tab::STATS) return !statsTurnRow(row);
  if (activeTabId == Tab::CAI_DAT) return true;
  return activeTabId == Tab::FOLDER && row >= 0 &&
         row < (folderRecursiveSearch ? listCount() : static_cast<int>(rowLabels.size())) &&
         !folderEntry(row).empty() && folderEntry(row).back() == '/';
}

bool HomeActivity::buttonHoldOpensRowMenu(const int row) const {
  if (activeTabId == Tab::FOLDER) return row >= 0 && row < listCount();
  return activeTabId == Tab::RECENT && row >= 0 && row < static_cast<int>(recentBooks.size());
}

void HomeActivity::onRowLongPress(const int index) {
  if (activeTabId == Tab::FOLDER) {
    showFolderActions(index);
    return;
  }
  if (activeTabId == Tab::RECENT) {
    showRecentActions(index, tenorchrome::kTouchShell
        ? recentCoverAt(index) : fui::Rect{});
    return;
  }
  // Touch: a held favourite offers Unpin and, with two or more, Reorder (the X3 holds Left or Right).
  if (tenorchrome::kTouchShell && !shell::isUgly() && activeTabId == Tab::FAVORITES && index >= 0 &&
      index < static_cast<int>(favoriteKeys.size())) {
    const StrId labels[] = {StrId::STR_UNPIN_FAVORITE, StrId::STR_REORDER};
    showRowMenu(labels, favoriteKeys.size() > 1 ? 2 : 1, [this, index](const int action) {
      if (action == 0) {
        queuePinToggle(index);
        return;
      }
      startActivityForResult(
          std::make_unique<ListOrderActivity>(renderer, mappedInput, rowLabels, index,
                                              [this, keys = favoriteKeys](const std::vector<int>& order) {
                                                std::vector<std::string> next;
                                                for (const int i : order) next.push_back(keys[i]);
                                                RenderLock lock(*this);
                                                favoriteSaveFailed = !menucustom::arrangePins(next);
                                                favoritesChanged();
                                              }),
          nullptr);
    }, ACTION_ROW, index);
    return;
  }
  UiListActivity::onRowLongPress(index);
}

unsigned long HomeActivity::touchHoldMs() const {
  return tenorchrome::kTouchShell && !shell::isUgly() && activeTabId == Tab::RECENT ? 700 : 0;
}

void HomeActivity::onRowAction(const fui::ActionEvent& event) {
  if (tenorchrome::kTouchShell && !shell::isUgly() && activeTabId == Tab::RECENT && event.longPress) {
    RenderLock lock(*this);
    onRowLongPress(event.value);
    return;
  }
  UiTabListActivity::onRowAction(event);
}

void HomeActivity::showFolderActions(const int index) {
  if (index < 0 || index >= listCount()) return;
  const bool folder = folderEntry(index).back() == '/';
  const StrId labels[] = {rowIsPinned(index) ? StrId::STR_UNPIN_FAVORITE : StrId::STR_PIN_FAVORITE,
                          folder ? StrId::STR_DELETE : StrId::STR_RENAME, StrId::STR_DELETE};
  const int count = folder ? 2 : 3;
  const std::string path = folderFullPath(index);
  const auto selected = [this, index, path, folder](const int action) {
    if (action == 0) {
      queuePinToggle(index);
    } else if (action == 1 && !folder) {
      fileactions::rename(*this, renderer, mappedInput, path, [this](const std::string&) { folderChanged(); });
    } else {
      fileactions::confirmDelete(*this, renderer, mappedInput, path, [this] { folderChanged(); });
    }
  };
  if (tenorchrome::kTouchShell) {
    showRowMenu(labels, count, selected, ACTION_ROW, index);
    return;
  }
  rowMenu.show(StrId::STR_FILENAME, labels, count, 0, selected);
  requestUpdate();
}

void HomeActivity::folderChanged() {
  {
    RenderLock lock(*this);
    rebuildRows(true);
    activeNav().selected = std::min<int>(activeNav().selected, listCount());
    activeNav().followOnBuild = true;
  }
  requestUpdate();
}

void HomeActivity::showOtherBookMenu() {
  const int count = static_cast<int>(recentBooks.size());
  if (count < 2) return;
  const int next = (shownRecent() + 1) % count;
  showRecentActions(next, app.publishedRect(ACTION_OTHER_BOOK, 0));
}

void HomeActivity::showRecentActions(const int index, const fui::Rect anchor) {
  if (index < 0 || index >= static_cast<int>(recentBooks.size())) return;
  const StrId labels[] = {rowIsPinned(index) ? StrId::STR_UNPIN_FAVORITE : StrId::STR_PIN_FAVORITE,
                          StrId::STR_REMOVE_RECENT_ACTION};
  const auto selected = [this, index](const int action) {
    if (action == 0) {
      queuePinToggle(index);
    } else if (action == 1 && index >= 0 && index < static_cast<int>(recentBooks.size())) {
      pendingRecentRemoval = recentBooks[index];
    }
  };
  if (!shell::isUgly()) {
    contextCover = anchor;
    const auto& book = recentBooks[index];
    const std::string title = book.title.empty() ? book.path.substr(book.path.find_last_of('/') + 1) : book.title;
    const fui::Rect ring{static_cast<int16_t>(anchor.x - 7), static_cast<int16_t>(anchor.y - 7),
                         static_cast<int16_t>(anchor.width + 14), static_cast<int16_t>(anchor.height + 14)};
    rowMenu.showContext(title.c_str(), ring, labels, rowIsPinned(index), selected,
                       [this](fui::Rect menu) { drawContextBackground(menu); });
  } else if (anchor.height > 0) {
    rowMenu.showAnchored(anchor, labels, 2, selected);
  } else {
    rowMenu.show(StrId::STR_HOME_TAB_RECENT, labels, 2, 0, selected);
  }
  requestUpdate();
}

fui::Rect HomeActivity::recentCoverAt(const int index) {
  if (index == shownRecent()) return heldCover;
  const auto layout = recentLayout();
  for (int slot = 0; slot < layout.shelfCount; ++slot) {
    if (layout.shelfBooks[slot] != index) continue;
    const auto size = cardCoverSize(index);
    const auto& cell = layout.shelf[slot];
    const auto fit = homeCoverFit(cell.w, cell.h, size.width, size.height);
    return {static_cast<int16_t>(24 + slot * 152 + (128 - fit.first) / 2),
            static_cast<int16_t>(cell.y + (cell.h - fit.second) / 2),
            static_cast<int16_t>(fit.first), static_cast<int16_t>(fit.second)};
  }
  return app.publishedRect(ACTION_OTHER_BOOK, 0);
}

void HomeActivity::rememberContextLine(int font, int x, int y, const char* text, EpdFontFamily::Style style) {
  if (tenorchrome::kTouchShell || shell::isUgly() || !text || !*text) return;
  if (contextTextLineCount >= contextTextLines.size()) return;
  const int top = renderer.getTextInkTop(font, text, style);
  const int bottom = renderer.getTextInkBottom(font, text, style);
  const fui::Rect line{fui::clampI16(x), fui::clampI16(y + top),
                       fui::clampI16(renderer.getTextWidth(font, text, style)), fui::clampI16(bottom - top)};
  contextTextLines[contextTextLineCount++] = line;
#ifdef SIMULATOR
  LOG_INF("HOME", "context text=%d,%d,%d,%d", line.x, line.y, line.width, line.height);
#endif
}

void HomeActivity::rememberContextStatGroup(size_t first, int x, int barY, int width) {
  const fui::Rect bar{fui::clampI16(x), fui::clampI16(barY), fui::clampI16(width),
                      static_cast<int16_t>(barY >= 0 ? HOME_STATS_BAR_H : 0)};
  mergeContextMenuTextRects(contextTextLines.data(), contextTextLineCount, first, bar);
}

void HomeActivity::drawContextBackground(const fui::Rect menu) {
  if (!tenorchrome::kTouchShell) {
    clearContextMenuTextRects(renderer, menu, contextTextLines.data(), contextTextLineCount);
    return;
  }
  std::array<fui::Rect, 4> covers{};
  covers[0] = heldCover;
  const auto layout = recentLayout();
  for (int slot = 0; slot < layout.shelfCount; ++slot)
    covers[slot + 1] = recentCoverAt(layout.shelfBooks[slot]);
  clearContextMenuLines(renderer, menu, covers, tenorchrome::contentTop(),
                        tenorchrome::footBackTop(renderer.getScreenHeight()) - 8);
  renderer.drawRoundedRect(contextCover.x - 7, contextCover.y - 7, contextCover.width + 14,
                           contextCover.height + 14, 3, tenorradius::cover(contextCover.width) + 7, true);
}

void HomeActivity::showRecentRemovalConfirmation(RecentBook book) {
  recentRemovalFailed = false;
  const std::string body = book.title.empty() ? book.path : book.title;
  auto confirmation = makeUniqueNoThrow<ConfirmationActivity>(
      renderer, mappedInput, tr(STR_REMOVE_FROM_RECENTS), body,
      ConfirmationContent{tr(STR_ALERT_RECENT_QUESTION), body + " " + tr(STR_ALERT_RECENT_MESSAGE_SUFFIX),
                          tr(STR_ALERT_RECENT_ACTION_SHORT), tr(STR_ALERT_RECENT_ACTION_LONG),
                          tr(STR_ALERT_RECENT_TOUCH_MESSAGE), true});
  if (!confirmation) {
    LOG_ERR("HOME", "OOM: remove recent confirmation");
    recentRemovalFailed = true;
    requestUpdate();
    return;
  }
  const std::string path = std::move(book.path);
  startActivityForResult(std::move(confirmation), [this, path](const ActivityResult& result) {
    if (result.isCancelled) return;
    const bool removed = RECENT_BOOKS.removeByPath(path);
    {
      RenderLock lock(*this);
      recentRemovalFailed = !removed;
      if (removed) {
        const int oldRing = ringPos();
        loadRecentBooks();
        rebuildRows(false);
        activeNav().selected = recentBooks.empty() ? 0 : std::min(oldRing, static_cast<int>(recentBooks.size()));
        activeNav().top = 0;
        activeNav().followOnBuild = true;
        cardRecordsRead = cardRecordsFound = 0;
        cardQuotesPicked = 0;
        thumbWanted = -1;
        freeCoverBuffer();
        coverRendered = false;
        heldCover = {};
      }
    }
    requestUpdate(true);
  });
}

std::string HomeActivity::favoriteKey(int row) const {
  if (activeTabId == Tab::RECENT && row >= 0 && row < static_cast<int>(recentBooks.size()))
    return filefavorites::keyFor(recentBooks[row].path, false);
  if (activeTabId == Tab::FOLDER && row >= 0 && row < listCount()) {
    if (lastFolderAt(row)) return {};
    auto path = folderFullPath(row);
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
  if (activeTabId == Tab::FOLDER && row >= 0 && row < listCount()) {
    if (lastFolderAt(row)) return false;
    auto path = folderFullPath(row);
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
  setPageScrollRegion(view.top, view.bottom, view.length, view.offset);

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
  if (view.offset > 0)
    tenorchrome::fadeBand(renderer, view.top, BAND, true, 0, renderer.getScreenWidth() - tenorchrome::FOOT_BACK_X);
  if (view.offset < view.length - viewHeight)
    tenorchrome::fadeBand(renderer, view.bottom - BAND, BAND, false, 0,
                         renderer.getScreenWidth() - tenorchrome::FOOT_BACK_X);
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

HomeActivity::CoverSize HomeActivity::cardCoverSize(const int index) {
  const uint8_t bit = static_cast<uint8_t>(1u << index);
  if (cardCoverSizesRead & bit) return cardCoverSizes[index];
  cardCoverSizesRead |= bit;
  cardCoverSizes[index] = {};
  const auto& book = recentBooks[index];
  if (book.coverBmpPath.empty()) return {};
  ImageDimensions dims{};
  if (FsHelpers::hasEpubExtension(book.path)) {
    Epub epub(book.path, "/.crosspoint");
    std::string href;
    if (coverref::load(epub.getCachePath(), href) && href.empty()) return {};
    if (epub.readCoverDimensions(href, dims)) {
      cardCoverSizes[index] = {dims.width, dims.height};
      return cardCoverSizes[index];
    }
  }
  HalFile file;
  if (Storage.openFileForRead("HOME", UITheme::getCoverThumbPath(book.coverBmpPath, HOME_CARD_COVER_H), file)) {
    Bitmap bitmap(file);
    if (bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() > 0 && bitmap.getHeight() > 0)
      cardCoverSizes[index] = {bitmap.getWidth(), bitmap.getHeight()};
    file.close();
  }
  return cardCoverSizes[index];
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

// Glyph boxes can include blank edge rows; use the BW ink already decoded by the renderer.
static HomeStatsBounds cardStatInkBounds(const GfxRenderer& renderer, const int font, const char* text,
                                         const EpdFontFamily::Style style) {
  const HomeStatsBounds fallback{renderer.getTextInkTop(font, text, style),
                                 renderer.getTextInkBottom(font, text, style)};
  const auto& families = renderer.getFontMap();
  const auto found = families.find(font);
  if (found == families.end()) return fallback;
  const auto* data = found->second.getData(style);
  HomeStatsBounds ink{INT32_MAX, 0};
  const auto* cursor = reinterpret_cast<const unsigned char*>(text);
  while (const auto cp = utf8NextCodepoint(&cursor)) {
    const auto* glyph = found->second.getGlyph(cp, style);
    if (!glyph) return fallback;
    if (!glyph->width || !glyph->height) continue;
    const auto* bitmap = renderer.getGlyphBitmap(data, glyph);
    if (!bitmap) return fallback;
    for (int y = 0; y < glyph->height; ++y) {
      for (int x = 0; x < glyph->width; ++x) {
        const int bit = y * glyph->width + x;
        const int value = data->is2Bit ? (bitmap[bit / 4] >> (6 - 2 * (bit % 4))) & 3
                                      : (bitmap[bit / 8] >> (7 - bit % 8)) & 1;
        if (!value) continue;
        ink.top = std::min(ink.top, data->ascender - glyph->top + y);
        ink.bottom = std::max(ink.bottom, data->ascender - glyph->top + y + 1);
      }
    }
  }
  return ink.top == INT32_MAX ? fallback : ink;
}

static constexpr StrId CARD_STAT_LABELS[HOME_STAT_COUNT] = {
    StrId::STR_RECENT_STAT_READ, StrId::STR_RECENT_STAT_FINISH, StrId::STR_RECENT_STAT_TOTAL,
    StrId::STR_RECENT_STAT_AVERAGE, StrId::STR_RECENT_STAT_DAYS, StrId::STR_STATS_TURNS};

// Label in the subtitle face, value in the body face (bold), stepping down one size when the value is
// wider than the column, as in the approved drawing. Returns the top of the progress bar, -1 when
// none is drawn. Six groups extend to the cover edge when their record has data.
int HomeActivity::drawCardStats(const HomeCardLayout& card, HomeStatsBounds* bounds) {
  const bool secondary = tenorchrome::kTouchShell ? SETTINGS.uiTextSize > 0 : SETTINGS.uiTextSize == 2;
  const int labelFont = secondary ? SMALL_FONT_ID : UI_10_FONT_ID;
  const int valueFont = secondary ? UI_10_FONT_ID : UI_12_FONT_ID;
  const int fallbackFont = secondary ? SMALL_FONT_ID : UI_10_FONT_ID;
  const int width = card.statsRight - card.statsX;
  HomeStatsInput in;
  in.top = card.statsTop;
  in.bottom = card.statsBottom ? card.statsBottom : card.coverY + card.coverH;
  in.labelLineHeight = renderer.getLineHeight(labelFont);
  in.valueLineHeight = renderer.getLineHeight(valueFont);
  in.valueTail = in.valueLineHeight - renderer.getFontAscenderSize(valueFont);
  in.rows = cardStats.rows;
  in.compact = tenorchrome::kTouchShell;
  in.groupGap = card.statsGap;
  if (in.compact && bounds) in.rows = (1u << HOME_STAT_COUNT) - 1;
  int fonts[HOME_STAT_COUNT];
  std::string first[HOME_STAT_COUNT], second[HOME_STAT_COUNT];
  std::vector<std::string> labels[HOME_STAT_COUNT];
  for (int row = 0; row < HOME_STAT_COUNT; ++row) {
    if (!(in.rows & (1u << row))) continue;
    if (in.compact) {
      labels[row] = renderer.wrappedText(labelFont, I18N.get(CARD_STAT_LABELS[row]), width, 2);
      in.labelHeights[row] = static_cast<int>(labels[row].size()) * in.labelLineHeight;
    }
    static constexpr const char* samples[] = {"15%", ">365 ngày", "10h 8m", "2h 1m/ngày", "5", "24"};
    first[row] = cardStats.values[row];
    const char* sizingValue = in.compact ? samples[row] : first[row].c_str();
    fonts[row] = cardStatValueWidth(renderer, valueFont, sizingValue) <= width ? valueFont : fallbackFont;
    if (in.compact && bounds) {
      // The frame is measured on samples so the cover keeps its size from day to day. A real value
      // that wraps where its sample does not would overflow that frame, so it is measured as it is.
      // A book with no numbers yet shows "Chưa ghi nhận" alone in a frame sized for 6 rows, where its
      // 2nd line has room: it is not measured, so the cover does not change size once numbers arrive.
      const bool realWraps = cardStats.recorded && !first[row].empty() &&
                             cardStatValueWidth(renderer, fonts[row], first[row].c_str()) > width;
      if (!realWraps || cardStatValueWidth(renderer, fonts[row], samples[row]) > width) first[row] = samples[row];
    }
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
    if (in.compact) {
      const char* label = labels[row].empty() ? I18N.get(CARD_STAT_LABELS[row]) : labels[row].front().c_str();
      const char* lastLabel = labels[row].empty() ? label : labels[row].back().c_str();
      const auto labelInk = cardStatInkBounds(renderer, labelFont, label, EpdFontFamily::REGULAR);
      const auto lastLabelInk = cardStatInkBounds(renderer, labelFont, lastLabel, EpdFontFamily::REGULAR);
      in.labelTops[row] = labelInk.top;
      in.labelHeights[row] = (labels[row].empty() ? 0 : static_cast<int>(labels[row].size() - 1) * in.labelLineHeight) +
                            lastLabelInk.bottom - labelInk.top;
      int firstTop = INT32_MAX;
      compactstats::runs(first[row].c_str(), [&](const std::string& part, const bool number) {
        firstTop = std::min(firstTop, cardStatInkBounds(renderer, fonts[row], part.c_str(),
            number ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR).top);
      });
      if (firstTop == INT32_MAX) firstTop = 0;
      in.valueTops[row] = firstTop;
      const auto cellInk = cardStatInkBounds(renderer, fonts[row], samples[row], EpdFontFamily::BOLD);
      in.valueHeights[row] = (second[row].empty() ? 0 : lineHeight) + cellInk.bottom - cellInk.top;
    }
  }
  const auto column = homeStatsLayout(in);
  if (bounds) {
    bounds->top = INT32_MAX;
    bounds->bottom = 0;
    for (int row = 0; row < HOME_STAT_COUNT; ++row) {
      if (!(column.rows & (1u << row))) continue;
      if (bounds->top == INT32_MAX) {
        const char* label = labels[row].empty() ? I18N.get(CARD_STAT_LABELS[row]) : labels[row].front().c_str();
        bounds->top = column.labelY[row] + cardStatInkBounds(renderer, labelFont, label, EpdFontFamily::REGULAR).top;
      }
      if (column.rows >> (row + 1)) continue;
      int lastBottom = 0;
      const auto& lastLine = second[row].empty() ? first[row] : second[row];
      compactstats::runs(lastLine.c_str(), [&](const std::string& part, const bool number) {
        lastBottom = std::max(lastBottom, cardStatInkBounds(renderer, fonts[row], part.c_str(),
            number ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR).bottom);
      });
      bounds->bottom = column.valueY[row] + (second[row].empty() ? 0 : renderer.getLineHeight(fonts[row])) + lastBottom;
    }
    return column.barY;
  }
  for (int row = 0; row < HOME_STAT_COUNT; ++row) {
    if (!(column.rows & (1u << row))) continue;
    const size_t groupStart = contextTextLineCount;
    if (labels[row].empty()) {
      rememberContextLine(labelFont, card.statsX, column.labelY[row], I18N.get(CARD_STAT_LABELS[row]));
      renderer.drawText(labelFont, card.statsX, column.labelY[row], I18N.get(CARD_STAT_LABELS[row]));
    } else {
      int y = column.labelY[row];
      for (const auto& line : labels[row]) {
        rememberContextLine(labelFont, card.statsX, y, line.c_str());
        renderer.drawText(labelFont, card.statsX, y, line.c_str());
        y += in.labelLineHeight;
      }
    }
    const auto drawValue = [&](const std::string& value, const int y) {
      rememberContextLine(fonts[row], card.statsX, y, value.c_str(), EpdFontFamily::BOLD);
      int x = card.statsX;
      compactstats::runs(value.c_str(), [&](const std::string& part, const bool number) {
        const auto style = number ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
        renderer.drawText(fonts[row], x, y, part.c_str(), true, style);
        x += cardStatRunWidth(renderer, fonts[row], part, style);
      });
    };
    drawValue(first[row], column.valueY[row]);
    if (!second[row].empty()) drawValue(second[row], column.valueY[row] + renderer.getLineHeight(fonts[row]));
    rememberContextStatGroup(groupStart, card.statsX,
                             row == HOME_STAT_READ && cardStats.recorded ? column.barY : -1, width);
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
  contextTextLineCount = 0;
  if (coverBufferBook != shownRecent() || coverBufferStyle != SETTINGS.homeStyle) touchThumbCount = 0;
  if (touchThumbCount > 0 && thumbWanted.load() < 0) {
    thumbWantedAtMs = millis();
    thumbWanted = static_cast<int8_t>(shownRecent());
  }
  const auto layout = recentLayout();
  if (layout.style != HomeRecentStyle::Stats) {
    drawTouchRecent(layout);
    return;
  }
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
  if (tenorchrome::x3ScreenLayoutEnabled()) {
    const auto layout = homeScreenLayout();
    in.top = layout.bodyRect.y;
    in.bottom = layout.bodyRect.bottom();
    if (const char* info = homeInfoText()) in.bottom -= 8 + tenorchrome::tipHeight(renderer, info, 3);
  }
  in.titleLineHeight = renderer.getLineHeight(UI_TITLE_FONT_ID);  // the cover is sized for the title's own face
  in.titleLines = 2;
  in.authorLineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  in.excerptLineHeight = renderer.getLineHeight(serifFont);
  in.rowLineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  std::vector<std::string> otherTitleLines;
  if (recentBooks.size() > 1) {
    const auto& next = recentBooks[(shown + 1) % recentBooks.size()];
    const auto title = next.title.empty() ? next.path.substr(next.path.find_last_of('/') + 1) : next.title;
    const int room = renderer.getScreenWidth() - 80 -
                     renderer.getTextWidth(UI_10_FONT_ID, tr(STR_RECENT_OTHER_BOOK)) - 16;
    otherTitleLines = renderer.wrappedText(UI_10_FONT_ID, title.c_str(), room, 2, EpdFontFamily::BOLD);
    in.rowLines = std::max(1, static_cast<int>(otherTitleLines.size()));
  }
  const int statLabelFont = (tenorchrome::kTouchShell ? SETTINGS.uiTextSize > 0 : SETTINGS.uiTextSize == 2) ? SMALL_FONT_ID : UI_10_FONT_ID;
  const int statValueFont = (tenorchrome::kTouchShell ? SETTINGS.uiTextSize > 0 : SETTINGS.uiTextSize == 2) ? UI_10_FONT_ID : UI_12_FONT_ID;
  for (const auto label : CARD_STAT_LABELS)
    in.statsMinWidth = std::max(in.statsMinWidth, renderer.getTextWidth(statLabelFont, I18N.get(label)));
  // Stable per board/tier: a minute changing never changes cover geometry or a saved-card key.
  in.statsMinWidth = std::max(in.statsMinWidth, cardStatValueWidth(renderer, statValueFont, "999h 59m"));
  char averageSample[64];
  snprintf(averageSample, sizeof(averageSample), tr(STR_RECENT_STAT_PER_DAY), SETTINGS.uiTextSize ? "999h 59m" : "1h 21m");
  in.statsMinWidth = std::max(in.statsMinWidth, cardStatValueWidth(renderer, statValueFont, averageSample));
  in.statsMinWidth += 8;
  // Medium touch text wraps in the default column instead of shrinking the cover for long samples.
  if (tenorchrome::kTouchShell && SETTINGS.uiTextSize == 1)
    in.statsMinWidth = std::min(in.statsMinWidth, renderer.getScreenWidth() / 3 - 4);
  // A fixed budget for two wrapped duration values leaves geometry independent of book time.
  in.minStatsHeight = 2 + HOME_STAT_COUNT * (renderer.getLineHeight(statLabelFont) +
                                           renderer.getLineHeight(statValueFont)) + 12 +
                      2 * std::max(0, 2 * renderer.getLineHeight((tenorchrome::kTouchShell ? SETTINGS.uiTextSize > 0 : SETTINGS.uiTextSize == 2) ? SMALL_FONT_ID : UI_10_FONT_ID) -
                                      renderer.getLineHeight(statValueFont));
  const auto size = tenorchrome::kTouchShell ? cardCoverSize(shown) : CoverSize{};
  const bool alignCover = tenorchrome::kTouchShell && size.width > 0 && size.height > 0;
  if (alignCover) {
    in.statsMinWidth = 0;
    for (int row = 0; row < HOME_STAT_COUNT; ++row) {
      in.statsMinWidth = std::max(in.statsMinWidth, renderer.getTextWidth(statLabelFont, I18N.get(CARD_STAT_LABELS[row])));
      in.statsMinWidth = std::max(in.statsMinWidth, cardStatValueWidth(renderer, statValueFont, row == HOME_STAT_AVERAGE ? "2h 1m/ngày" : row == HOME_STAT_FINISH ? ">365 ngày" : "10h 8m"));
    }
    in.statsMinWidth = std::min(in.statsMinWidth, renderer.getScreenWidth() / 2);
  }
  auto frame = homeCardLayout(in);
  if (tenorchrome::kTouchShell) frame.textW = renderer.getScreenWidth() - 40;
  // The full-width title above the cover needs the stats column to start below it, cover or not.
  if (tenorchrome::kTouchShell && in.metadataAboveCover)
    frame.statsTop = in.top + 2 * in.titleLineHeight + 2 + in.authorLineHeight + 18;
  if (alignCover) {
    frame.statsRight = renderer.getScreenWidth() - 16;
    frame.statsBottom = frame.ruleY - 15;
    frame.statsX = frame.statsRight - in.statsMinWidth;
    if (!in.metadataAboveCover)
      frame.statsBottom -= 18 + 2 * in.titleLineHeight + 2 + in.authorLineHeight + 7 + in.excerptLineHeight;
    HomeStatsBounds ink;
    drawCardStats(frame, &ink);
    {
      const int widthLimit = frame.statsRight - frame.coverX - 22 - in.statsMinWidth;
      const int heightLimit = size.height > size.width
                                  ? std::min(widthLimit * size.height / size.width, frame.statsBottom - ink.top)
                                  : frame.statsBottom - ink.top;
      const int excess = ink.bottom - ink.top - heightLimit;
      if (excess > 0) {
        frame.statsGap = std::max(2, frame.statsGap - (excess + HOME_STAT_COUNT - 2) / (HOME_STAT_COUNT - 1));
        drawCardStats(frame, &ink);
      }
    }
    frame = homeCoverAlignedToStats(frame, ink, size.width, size.height, in.statsMinWidth);
    frame.textW = size.width > size.height ? frame.coverW : renderer.getScreenWidth() - 40;
    if (!in.metadataAboveCover) {
      const int shift = frame.coverY + frame.coverH + 18 - frame.titleY;
      frame.titleY += shift;
      frame.authorY += shift;
      frame.excerptY += shift;
    }
    LOG_INF("HOME", "Card geometry cover=%d,%d,%d,%d ink=%d,%d stats=%d,%d source=%d,%d",
            frame.coverX, frame.coverY, frame.coverW, frame.coverH, ink.top, ink.bottom,
            frame.statsX, frame.statsRight - 1, size.width, size.height);
  }
  cardThumbWidth = alignCover ? frame.coverW : HOME_CARD_COVER_W;
  cardThumbHeight = alignCover ? frame.coverH : HOME_CARD_COVER_H;
  heldCover = {static_cast<int16_t>(frame.coverX), static_cast<int16_t>(frame.coverY), static_cast<int16_t>(frame.coverW),
               static_cast<int16_t>(frame.coverH)};
  if (kInkX3 && homeInkEnabled()) {
    cardThumbWidth = frame.coverW;
    cardThumbHeight = frame.coverH;
  }
  if (homeInkEnabled()) {
    inkSlotCount = 1;
    inkSlots[0] = {{frame.coverX, frame.coverY, frame.coverW, frame.coverH}, shown};
    LOG_INF("HOME", "tenor/ink frame cover=%d,%d,%d,%d", frame.coverX, frame.coverY, frame.coverW, frame.coverH);
  }
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
  // CJK titles stay on the body font: its SD fallback covers them, the title font has none.
  const int titleFont = homeExcerptUsesUiFont(title.c_str()) ? UI_12_FONT_ID : UI_TITLE_FONT_ID;
  const auto titleLines = renderer.wrappedText(titleFont, title.c_str(), frame.textW, 2, EpdFontFamily::BOLD);
  in.titleLines = std::max(1, static_cast<int>(titleLines.size()));
  in.titleDrawLineHeight = renderer.getLineHeight(titleFont);
  auto card = alignCover ? frame : homeCardLayout(in);
  if (!alignCover) card.statsTop = frame.statsTop;
  if (alignCover) {
    card.authorY = card.titleY + in.titleLines * in.titleDrawLineHeight + 2;
    card.excerptY = card.authorY + in.authorLineHeight + 7;
    const int bottom = in.metadataAboveCover ? card.coverY - 7 : card.ruleY - 15;
    card.excerptLines = std::clamp((bottom - card.excerptY) / in.excerptLineHeight, 0, 1);
  }
  const bool cjkExcerpt = homeExcerptUsesUiFont(excerpt.c_str());
  const int quoteFont = excerpt.empty() ? SMALL_FONT_ID : cjkExcerpt ? UI_12_FONT_ID : serifFont;
  const auto quoteStyle = excerpt.empty() || cjkExcerpt ? EpdFontFamily::REGULAR : EpdFontFamily::ITALIC;
  if (!tenorchrome::kTouchShell && !shell::isUgly()) {
    int lineY = card.titleY;
    for (const auto& line : titleLines) {
      rememberContextLine(titleFont, card.textX, lineY, line.c_str(), EpdFontFamily::BOLD);
      lineY += in.titleDrawLineHeight;
    }
    const auto authorLine = renderer.truncatedText(UI_10_FONT_ID, author, card.textW);
    rememberContextLine(UI_10_FONT_ID, card.textX, card.authorY, authorLine.c_str());
    lineY = card.excerptY;
    for (const auto& line : renderer.wrappedText(quoteFont, quote.c_str(), card.textW, card.excerptLines, quoteStyle)) {
      rememberContextLine(quoteFont, card.textX, lineY, line.c_str(), quoteStyle);
      lineY += renderer.getLineHeight(quoteFont);
    }
  }
#ifdef TENOR_UI_ACCEPTANCE
  const uint32_t restoreStartedUs = micros();
#endif
  if (coverBufferStored && coverBufferBook == shown && restoreCoverBuffer()) {
#ifdef TENOR_UI_ACCEPTANCE
    LOG_INF("HOME_PROBE", "card_restore_us=%lu cache_bytes=%u", static_cast<unsigned long>(micros() - restoreStartedUs),
            static_cast<unsigned>(coverBufferSize));
#endif
    drawCardStats(frame);
    drawOtherBookRow(shown, frame.ruleY, frame.rowY, otherTitleLines);
    return;
  }
  const uint32_t started = millis();
#ifdef TENOR_UI_ACCEPTANCE
  const uint32_t cardStartedUs = micros();
#endif
  LOG_INF("HOME", "Home build start style=0");
#ifdef TENOR_PRESS_PROBE
  const uint32_t quotedMs = millis();
#endif
  // The cover and the lines under it depend on these alone (the cover's height and the title's
  // line count do not move the cover), so the keys say whether a card file still shows this card,
  // or at least its cover.
  const std::string thumbPath = book.coverBmpPath.empty() ? std::string()
      : (homeInkEnabled() ? touchCoverPath(book.coverBmpPath, cardThumbWidth, cardThumbHeight)
                                  : UITheme::getCoverThumbPath(book.coverBmpPath, cardThumbHeight));
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
    drawOtherBookRow(shown, frame.ruleY, frame.rowY, otherTitleLines);
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
  int y = card.titleY;
  for (const auto& line : titleLines) {
    renderer.drawText(titleFont, card.textX, y, line.c_str(), true, EpdFontFamily::BOLD);
    y += in.titleDrawLineHeight;
  }
  renderer.drawText(UI_10_FONT_ID, card.textX, card.authorY,
                    renderer.truncatedText(UI_10_FONT_ID, author, card.textW).c_str());
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
  const int fallbackHeights[] = {cardThumbHeight, HOME_CARD_COVER_H, UITheme::getInstance().getMetrics().homeCoverHeight};
  // The X3 without its tenor/ink cover yet draws the reader's 1 bit thumbnail: the ink cover of a
  // baseline JPEG needs a 32 KB inflate window in one block, which Home after reading does not have.
  for (int attempt = 0; attempt < (homeInkEnabled() && tenorchrome::kTouchShell ? 1 : 3); ++attempt) {
    const int height = fallbackHeights[attempt];
    if (image || book.coverBmpPath.empty()) break;
    HalFile file;
    const auto path = (homeInkEnabled() && attempt == 0)
                          ? touchCoverPath(book.coverBmpPath, cardThumbWidth, height)
                          : UITheme::getCoverThumbPath(book.coverBmpPath, height);
    if (homeInkEnabled() && attempt == 0) coverink::restore(path);
    if (!Storage.openFileForRead("HOME", path, file)) continue;
    if (attempt == 0) cardFileThumb = 1;
    Bitmap bitmap(file);
#ifdef TENOR_PRESS_PROBE
    LOG_INF("HOME", "CARD_COVER_START height=%d free=%u largest=%u", height, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    const uint32_t coverStarted = millis();
#endif
    if (bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() > 0 && bitmap.getHeight() > 0) {
      if (homeInkEnabled() && attempt == 0) {
        image = bitmap.getWidth() == card.coverW && bitmap.getHeight() == card.coverH &&
                renderer.drawBitmap(bitmap, card.coverX, card.coverY, card.coverW, card.coverH);
        if (!image) cardFileThumb = 0;
      } else if (!tenorchrome::kTouchShell) {
        image = renderer.drawBitmapCover(bitmap, card.coverX, card.coverY, card.coverW, card.coverH);
      }
    }
#ifdef TENOR_PRESS_PROBE
    LOG_INF("HOME", "CARD_COVER_END height=%d ms=%lu ok=%u free=%u largest=%u", height,
            static_cast<unsigned long>(millis() - coverStarted), image ? 1u : 0u,
            ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#endif
    if (image) coverHeight = height;
  }
  // A plain frame waits for the idle pass to write the placeholder or the book's cover: the card is not
  // kept, or its file would bring the plain frame back after the picture is on the card.
  const bool plainFrame = !image && !drawHolder(shown, coverless(shown), card.coverX, card.coverY, card.coverW, card.coverH);
  // Round the cover's corners by painting the page back over them, the pixels a rounded card
  // would not cover: a few hundred pixels, no second buffer, and the cached card keeps them.
  if (!coverKept)
    homeCoverCorners(renderer, card.coverX, card.coverY, card.coverW, card.coverH);
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
  if (coverBufferStored && !cardPath.empty() && !plainFrame) {
    cardFilePending = cardPath;
    cardFileCoverKey = coverKey;
    cardFileKey = key;
    cardFileCover = static_cast<int16_t>(coverHeight);
  }
  if (!cardFileThumb && !thumbPath.empty()) wantThumb(shown);
  if (fcm) fcm->releaseBuiltinPageCaches();  // the card is now a cached bitmap
  const int barY = drawCardStats(card);
  drawOtherBookRow(shown, card.ruleY, card.rowY, otherTitleLines);
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
  LOG_INF("HOME", "Home build end style=0");
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
    if (tenorchrome::kTouchShell) {
      touchCoverBuffer = HalMemory::allocatePsram(bytes);
      coverBuffer = touchCoverBuffer.get();
    } else coverBuffer = static_cast<uint8_t*>(heapfloor::take(bytes, "home-card"));
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
    coverBufferStyle = SETTINGS.homeStyle;
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
  if (coverless(index)) return;
  if (homeInkEnabled()) {
    queueTouchThumb(index, cardThumbWidth, cardThumbHeight);
    return;
  }
  if (!FsHelpers::hasEpubExtension(recentBooks[index].path)) return;
  const uint32_t id = tenorchrome::kTouchShell
                          ? fnv(fnv(fnv(2166136261u, recentBooks[index].path), &cardThumbWidth, sizeof(cardThumbWidth)),
                                &cardThumbHeight, sizeof(cardThumbHeight))
                          : fnv(fnv(2166136261u, recentBooks[index].path), &cardThumbHeight, sizeof(cardThumbHeight));
  for (const uint32_t tried : thumbTried)
    if (tried == id) return;
  thumbWantedAtMs = millis();
  thumbWanted = static_cast<int8_t>(index);
}

void HomeActivity::restoreHomeGhost([[maybe_unused]] const char* reason) {
  if (homeRefresh.count() == 0) return;
#ifdef TENOR_PRESS_PROBE
  const uint32_t started = millis();
  const unsigned swaps = homeRefresh.count();
#endif
  HalPowerManager::Lock fullSpeed;
  // A fast pass that drives every pixel clears the swaps' ghosts without the full waveform's flash.
  renderer.redriveNextRefresh(HalDisplay::FAST_REFRESH);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  homeRefresh.restored();
#ifdef TENOR_PRESS_PROBE
  LOG_INF("HOME", "HOME_GHOST_RESTORE waves=1 mode=fast_redrive rect=0,0,%d,%d ms=%lu swaps=%u reason=%s",
          renderer.getScreenWidth(), renderer.getScreenHeight(), static_cast<unsigned long>(millis() - started),
          swaps, reason);
#endif
}

// Leaving Home: the next screen's first pass drives every pixel instead of Home drawing itself again.
void HomeActivity::handOffHomeGhost() {
  if (homeRefresh.count() == 0) return;
  renderer.redriveNextRefresh(HalDisplay::FAST_REFRESH);
  homeRefresh.restored();
#ifdef TENOR_PRESS_PROBE
  LOG_INF("HOME", "HOME_GHOST_HANDOFF mode=fast_redrive");
#endif
}

void HomeActivity::tickHomeGhost() {
  RenderLock lock(RenderLock::TryTake{});
  if (!lock.acquired() || homeRefresh.count() == 0) return;
  uint8_t contacts = 0;
  int x = 0, y = 0;
  const bool touch = mappedInput.touchContactsAt(contacts, x, y) && contacts > 0;
  bool input = touch || gpio.wasTouchActivity() || mappedInput.wasAnyPressed() || mappedInput.wasAnyReleased();
  for (int key = 0; !input && key <= static_cast<int>(MappedInputManager::Button::ScreenDown); ++key)
    input = mappedInput.isPressed(static_cast<MappedInputManager::Button>(key));
  if (homeRefresh.idle(millis(), activeTabId == Tab::RECENT && !rowMenu.isActive(), input))
    restoreHomeGhost("idle");
}

void HomeActivity::onTick() {
  tickHomeGhost();
  if (wakeStatePending.exchange(false)) saveAppState();
  // A tick that wrote a placeholder leaves the idle step for the next one: the redraw comes first.
  const bool holderDrawn = tickHolders();
  const int index = thumbWanted.load();
  if (index < 0) {
    if (!holderDrawn) idleStep();
    return;
  }
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
  HalPowerManager::Lock fullSpeed;
  if (activeTabId != Tab::RECENT || index != shownRecent()) return;
  if (mappedInput.wasAnyPressed() || mappedInput.wasAnyReleased() || gpio.rawInputActive()) {
    thumbWantedAtMs = millis();
    thumbWanted = static_cast<int8_t>(index);
    return;
  }
  if (homeInkEnabled()) {
    const auto requests = touchThumbs;
    const int count = std::min(touchThumbCount, 1);
    touchThumbCount -= count;
    for (int slot = 0; slot < touchThumbCount; ++slot) touchThumbs[slot] = requests[slot + count];
    if (touchThumbCount > 0) thumbWanted = static_cast<int8_t>(index);
    freeCoverBuffer();
    coverRendered = false;
    bool wrote = false;
    for (int slot = 0; slot < count; ++slot) {
      const auto& q = requests[slot];
      const auto& path = recentBooks[q.book].path;
      thumbTried[thumbTriedNext++ % 5] = fnv(fnv(fnv(fnv(2166136261u, path), &q.width, sizeof(q.width)),
                                               &q.height, sizeof(q.height)), &homeGraySelected, sizeof(homeGraySelected));
      Epub epub(path, "/.crosspoint");
      std::string href;
      const bool ref = coverref::load(epub.getCachePath(), href);
      if (ESP.getFreeHeap() < (ref ? CARD_THUMB_REF_MIN_FREE_HEAP : CARD_THUMB_MIN_FREE_HEAP) ||
          ESP.getMaxAllocHeap() < CARD_THUMB_MIN_LARGEST_BLOCK) continue;
      const uint32_t started = millis();
      if (ref || epub.load(false, true)) {
        epub.generateThumbBmps(ref ? href : epub.getCoverHref(), &q.width, &q.height, 1,
                               homeInkEnabled(), homeGraySelected);
      }
      LOG_INF("HOME", "Home thumb book=%d size=%dx%d ms=%lu", q.book, q.width, q.height,
              static_cast<unsigned long>(millis() - started));
      wrote = Storage.exists(touchCoverPath(recentBooks[q.book].coverBmpPath, q.width, q.height).c_str()) || wrote;
    }
    // A cover that could not be made leaves the frame as it is: drawing it again is a refresh for nothing.
    thumbBatchWrote = thumbBatchWrote || wrote;
    if (touchThumbCount == 0 && thumbBatchWrote) {
      thumbBatchWrote = false;
      requestUpdate();
    }
    return;
  }
  const std::string path = recentBooks[index].path;
  thumbTried[thumbTriedNext++ % 5] = tenorchrome::kTouchShell
                                         ? fnv(fnv(fnv(2166136261u, path), &cardThumbWidth, sizeof(cardThumbWidth)),
                                               &cardThumbHeight, sizeof(cardThumbHeight))
                                         : fnv(fnv(2166136261u, path), &cardThumbHeight, sizeof(cardThumbHeight));
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
    if (ref) {
      if (tenorchrome::kTouchShell) {
        epub.generateThumbBmps(href, &cardThumbWidth, &cardThumbHeight, 1);
      } else {
        const int heights[] = {cardThumbHeight, UITheme::getInstance().getMetrics().homeCoverHeight};
        epub.generateThumbBmps(href, heights, 2);
      }
    } else if (epub.load(false, true)) {
      if (tenorchrome::kTouchShell) {
        epub.generateThumbBmps(epub.getCoverHref(), &cardThumbWidth, &cardThumbHeight, 1);
      } else {
        const int heights[] = {cardThumbHeight, UITheme::getInstance().getMetrics().homeCoverHeight};
        epub.generateThumbBmps(heights, 2);
      }
    }
    requestUpdate();
  }
}

namespace {
// The cover in the book, for the idle step: read through the book's zip, a stop asked at each piece.
struct BookCover {
  Epub& epub;
  const std::string& href;
  HomeActivity* home;
};
struct StopPrint : Print {
  const idlework::Stop& stop;
  bool stopped = false;
  explicit StopPrint(const idlework::Stop& s) : stop(s) {}
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t* p, size_t n) override {
    if (stop()) {
      stopped = true;
      return 0;
    }
    return take(p, n);
  }
  virtual size_t take(const uint8_t* p, size_t n) = 0;
};
progluma::Result sniffCover(void* user, progluma::Sniffer& sniffer, const idlework::Stop& stop) {
  auto& c = *static_cast<BookCover*>(user);
  if (!FsHelpers::hasJpgExtension(c.href)) return progluma::Result::Unsupported;
  struct Sink : StopPrint {
    progluma::Sniffer& sniffer;
    Sink(const idlework::Stop& s, progluma::Sniffer& f) : StopPrint(s), sniffer(f) {}
    // A short write ends the read once the frame is known.
    size_t take(const uint8_t* p, size_t n) override { return sniffer.feed(p, n) ? 0 : n; }
  } sink(stop, sniffer);
  const bool whole = c.epub.readItemContentsToStream(c.href, sink, 1024, true);
  if (sink.stopped) return progluma::Result::Stopped;
  // Read to its end without an answer, the cover is damaged; a failed read may pass.
  return sniffer.known() || whole ? progluma::Result::Done : progluma::Result::Io;
}
progluma::Result copyCover(void* user, const std::string& path, const idlework::Stop& stop) {
  auto& c = *static_cast<BookCover*>(user);
  HalFile file;
  if (!Storage.openFileForWrite("HOME", path, file)) return progluma::Result::Io;
  struct Sink : StopPrint {
    HalFile& file;
    Sink(const idlework::Stop& s, HalFile& f) : StopPrint(s), file(f) {}
    size_t take(const uint8_t* p, size_t n) override { return file.write(p, n); }
  } sink(stop, file);
  const bool whole = c.epub.readItemContentsToStream(c.href, sink, 8192);
  const bool closed = file.close();
  if (sink.stopped) return progluma::Result::Stopped;
  return whole && closed ? progluma::Result::Done : progluma::Result::Io;
}
}  // namespace

// Every tenor/ink slot this frame showed, for the idle step. The 1 bit branch (ugly shell, X3
// layout off) keeps its GrayThumb covers as they are.
void HomeActivity::queueIdleCovers() {
  if (!kInkX3) return;
  idleQueue.clear();
  idleQuietSince = millis();
  if (!homeInkEnabled()) return;
  for (int i = 0; i < inkSlotCount; ++i) {
    const auto& slot = inkSlots[i];
    if (slot.book < 0 || slot.book >= static_cast<int>(recentBooks.size())) continue;
    const auto& book = recentBooks[slot.book];
    if (book.coverBmpPath.empty() || !FsHelpers::hasEpubExtension(book.path)) continue;
    idlework::Job job;
    job.key = fnv(fnv(fnv(fnv(2166136261u, book.path), &slot.rect.w, sizeof(slot.rect.w)), &slot.rect.h,
                      sizeof(slot.rect.h)), &homeGraySelected, sizeof(homeGraySelected));
    job.run = coverStep;
    job.owner = this;
    job.arg[0] = static_cast<int16_t>(slot.book);
    job.arg[1] = static_cast<int16_t>(slot.rect.w);
    job.arg[2] = static_cast<int16_t>(slot.rect.h);
    job.arg[3] = homeGraySelected ? 1 : 0;
    idleQueue.push(job);
  }
}

// On the main task, while Home stands still: one step of the queue's first job.
void HomeActivity::idleStep() {
  if (!kInkX3) return;
  // A key pressed, let go or still held (a key held past 3 s has no edge left to see): not idle.
  if (mappedInput.wasAnyPressed() || mappedInput.wasAnyReleased() || gpio.rawInputActive()) {
    idleQuietSince = millis();
    return;
  }
  // Read before the lock: only the render task fills the queue, and it is read again under the lock.
  if (idleQueue.empty() || static_cast<int32_t>(millis() - idleRetryAt) < 0) return;
  const uint64_t clockUs = idleClockUs();
  // Home in front: the Recent tab, no menu over it.
  const bool front = activeTabId == Tab::RECENT && !rowMenu.isActive();
  const idlework::IdleInputs in{front ? idlework::Where::Home : idlework::Where::Elsewhere, millis() - idleQuietSince,
                                gpio.isUsbConnected(), static_cast<int>(powerManager.getBatteryPercentage()),
                                idleBudget.used(clockUs), idleBudget.stepsIn(clockUs)};
  if (idlework::verdict(in) != idlework::Why::Ok) return;
  // As the reader's index step: never wait for the render task; a frame in progress skips this pass.
  RenderLock lock(RenderLock::TryTake{});
  if (!lock.acquired() || idleQueue.empty()) return;
  // The loop drops the CPU to 10 MHz after 3 s without a key, exactly when this runs.
  HalPowerManager::Lock fullSpeed;
  idlework::Stop stop;
  stop.keyDown = [](void*) { return gpio.rawInputActive(); };
  stop.clockMs = [] { return static_cast<uint32_t>(millis()); };
  stop.startedMs = millis();
  idleOutcome = static_cast<uint8_t>(coverink::Outcome::Nothing);
  idleWhy = "";
#ifdef TENOR_PRESS_PROBE
  const size_t freeBefore = ESP.getFreeHeap();
#endif
  idleQueue.runFront(stop);
  const uint32_t ran = millis() - stop.startedMs;
  idleBudget.charge(idleClockUs(), ran);
  LOG_INF("HOME", "Idle cover outcome=%u ms=%lu", idleOutcome, static_cast<unsigned long>(ran));
#ifdef TENOR_PRESS_PROBE
  LOG_INF("HOME", "Idle cover why=%s", idleWhy);
  LOG_INF("HOME", "Idle cover heap usb=%u free=%u/%u largest=%u", in.usb ? 1u : 0u, static_cast<unsigned>(freeBefore),
          static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
#endif
  const auto outcome = static_cast<coverink::Outcome>(idleOutcome);
  if (outcome == coverink::Outcome::Sharp) requestUpdate();
  // A heap that would not hold the step: wait longer each time, up to 10 minutes.
  if (outcome == coverink::Outcome::NoHeap) {
    idleRetryAt = millis() + idleBackoffMs;
    idleBackoffMs = std::min<uint32_t>(idleBackoffMs * 2, 600000);
  } else {
    idleBackoffMs = 30000;
  }
}

// What the step takes is given back by the card first: its snapshot is read from its file again.
void HomeActivity::idleRelease() {
  freeCoverBuffer();
  coverRendered = false;
}

idlework::Step HomeActivity::coverStep(const idlework::Job& job, const idlework::Stop& stop) {
  auto& home = *static_cast<HomeActivity*>(job.owner);
  if (job.arg[0] >= static_cast<int>(home.recentBooks.size())) {
    home.idleWhy = "no-book";
    return idlework::Step::Done;
  }
  const auto& book = home.recentBooks[job.arg[0]];
  Epub epub(book.path, "/.crosspoint");
  // Where the reader left the cover's path; a book not opened since that began waits for it.
  std::string href;
  if (!coverref::load(epub.getCachePath(), href)) {
    home.idleWhy = "no-cover-ref";
    return idlework::Step::Done;
  }
  BookCover cover{epub, href, &home};
  coverink::Source source;
  source.sniff = sniffCover;
  source.copy = copyCover;
  source.release = [](void* user) { static_cast<BookCover*>(user)->home->idleRelease(); };
  source.user = &cover;
  const bool four = job.arg[3] != 0;
  const auto outcome = coverink::build(epub.getCachePath(), tenorink::cachePath(book.coverBmpPath, job.arg[1],
                                                                                job.arg[2], four),
                                       job.arg[1], job.arg[2], four, source, stop, &home.idleWhy);
  home.idleOutcome = static_cast<uint8_t>(outcome);
  return outcome == coverink::Outcome::Stopped || outcome == coverink::Outcome::NoHeap ? idlework::Step::Stopped
                                                                                         : idlework::Step::Done;
}

HomeRecentLayout HomeActivity::recentLayout() {
  const int shown = shownRecent();
  const auto size = tenorchrome::kTouchShell && shown >= 0 ? cardCoverSize(shown) : CoverSize{};
  const int serif = SETTINGS.uiTextSize == 1 ? NOTOSERIF_14_FONT_ID
                    : SETTINGS.uiTextSize == 2 ? NOTOSERIF_16_FONT_ID : NOTOSERIF_12_FONT_ID;
  return homeRecentLayout(tenorchrome::kTouchShell, SETTINGS.homeStyle, renderer.getScreenWidth(),
                          coverTileTop() - 4, tenorchrome::footBackTop(renderer.getScreenHeight()) - 8,
                          renderer.getLineHeight(UI_TITLE_FONT_ID), renderer.getLineHeight(UI_10_FONT_ID),
                          renderer.getLineHeight(SMALL_FONT_ID), renderer.getLineHeight(serif),
                          shown, static_cast<int>(recentBooks.size()), size.width, size.height);
}

void HomeActivity::queueTouchThumb(const int index, const int width, const int height) {
  if (!FsHelpers::hasEpubExtension(recentBooks[index].path)) return;
  const uint32_t id = fnv(fnv(fnv(fnv(2166136261u, recentBooks[index].path), &width, sizeof(width)),
                              &height, sizeof(height)), &homeGraySelected, sizeof(homeGraySelected));
  for (const auto tried : thumbTried) if (tried == id) return;
  for (int slot = 0; slot < touchThumbCount; ++slot)
    if (touchThumbs[slot].book == index) return;
  if (touchThumbCount >= static_cast<int>(touchThumbs.size())) return;
  if (touchThumbCount == 0) thumbBatchWrote = false;
  touchThumbs[touchThumbCount++] = {index, width, height};
  if (thumbWanted.load() < 0) {
    thumbWantedAtMs = millis();
    thumbWanted = static_cast<int8_t>(shownRecent());
  }
}

bool HomeActivity::homeInkEnabled() const {
  return !shell::isUgly() && ((tenorchrome::kTouchShell && HalMemory::getPsramHeap().totalBytes > 0) ||
                              (kInkX3 && tenorchrome::x3ScreenLayoutEnabled()));
}

bool HomeActivity::homeGrayWanted() const {
  auto heap = HalMemory::getDefaultHeap();
#ifdef SIMULATOR
  if (std::getenv("CROSSPOINT_SIM_INK_GRAY_FAIL")) heap.largestBlockBytes = 8179;
#endif
  return tenorink::homeGray(tenorchrome::kTouchShell, HalMemory::getPsramHeap().totalBytes > 0,
                            activeTabId == Tab::RECENT, shell::isUgly(),
                            renderer.grayscaleCapabilities(HalDisplay::GrayscaleMode::Absolute).supported(),
                            TENOR_INK_HOME_LEVELS, kInkX3 && kInkX3Gray && tenorchrome::x3ScreenLayoutEnabled(),
                            heap.freeBytes, heap.largestBlockBytes);
}

std::string HomeActivity::touchCoverPath(const std::string& cover, int width, int height) const {
  if (!homeInkEnabled()) return UITheme::getCoverThumbPath(cover, width, height);
  const auto path = tenorink::cachePath(cover, width, height, homeGraySelected);
  if (!homeGraySelected && !Storage.exists(path.c_str())) {
    // An existing four-level thumbnail is dithered by the BW renderer during an upgrade.
    const auto saved = tenorink::cachePath(cover, width, height, true);
    if (Storage.exists(saved.c_str())) return saved;
  }
  return path;
}

void HomeActivity::clearHomeGray() {
  if (!homeGrayOnGlass) return;
  // Same cleanup as reader/sleep: re-seed RAM from BW; SSD1677 keeps its gray-clear debt
  // so the next display promotes FAST to HALF/FULL and clears the ink on the glass.
  renderer.cleanupGrayscaleWithFrameBuffer();
  homeGrayOnGlass = false;
}

bool HomeActivity::showHomeGray() {
  if (!homeGraySelected || inkSlotCount == 0) return false;
  std::array<bool, 4> present{};
  std::array<std::string, 4> paths;  // the book's own cover, else the placeholder of the frame
  int found = 0;
  for (int i = 0; i < inkSlotCount; ++i) {
    const auto& slot = inkSlots[i];
    const auto& r = slot.rect;
    const auto& cover = recentBooks[slot.book].coverBmpPath;
    for (int attempt = 0; attempt < 2 && !present[i]; ++attempt) {
      if (attempt == 0 && cover.empty()) continue;
      if (attempt == 1 && !coverless(slot.book)) continue;
      paths[i] = attempt == 0 ? touchCoverPath(cover, r.w, r.h) : holderPath(r.w, r.h);
      auto file = Storage.open(paths[i].c_str());
      if (!file) continue;
      Bitmap bitmap(file);
      present[i] = bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() == r.w &&
                   bitmap.getHeight() == r.h && bitmap.getBpp() == 2;
    }
    if (present[i]) ++found;
  }
  if (!found) return false;
  if (kInkX3) {
    constexpr int stripRows = heapfloor::kGrayStripRows;
    const int rowBytes = renderer.getDisplayWidthBytes();
    const size_t bytes = size_t(rowBytes) * stripRows;
    // As the reader's strip: the heap floor is measured before the strip is asked for (the floor keeps
    // this block's room); while the strip is held only a failed allocation stops the gray pass.
    const auto before = HalMemory::getDefaultHeap();
    if (heapfloor::crosses(before.freeBytes, before.largestBlockBytes)) {
      LOG_INF("HOME", "tenor/ink gray skipped heap_floor free=%u largest=%u bytes=%u",
              static_cast<unsigned>(before.freeBytes), static_cast<unsigned>(before.largestBlockBytes),
              static_cast<unsigned>(bytes));
      homeGraySelected = false;
      return false;
    }
    struct StripWindowEnd {
      bool held = false;
      ~StripWindowEnd() { if (held) heapBigStripWindow(false); }
    } stripWindowEnd;
    auto scratch = makeUniqueNoThrow<uint8_t[]>(bytes);
    if (!scratch) { homeGraySelected = false; return false; }
    heapBigStripWindow(true);
    stripWindowEnd.held = true;
    const uint32_t started = millis();
    if (!renderer.displayGrayscaleBase(HalDisplay::GrayscaleMode::Absolute,
                                      cleanInitialRefresh ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH)) {
      homeGraySelected = false;
      return false;
    }
    bool ready = true;
    for (const auto plane : {GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB}) {
      renderer.setRenderMode(plane);
      for (int y = 0; y < renderer.getDisplayHeight() && ready; y += stripRows) {
        const int rows = std::min(stripRows, int(renderer.getDisplayHeight()) - y);
        std::memcpy(scratch.get(), renderer.getFrameBuffer() + size_t(y) * rowBytes, size_t(rows) * rowBytes);
        renderer.beginStripTarget(scratch.get(), y, rows);
        for (int i = 0; i < inkSlotCount; ++i) {
          if (!present[i]) continue;
          const auto& slot = inkSlots[i];
          const auto& r = slot.rect;
          auto file = Storage.open(paths[i].c_str());
          Bitmap bitmap(file);
          if (!file || bitmap.parseHeaders() != BmpReaderError::Ok ||
              !renderer.drawBitmap(bitmap, r.x, r.y, r.w, r.h)) { ready = false; break; }
          homeCoverCorners(renderer, r.x, r.y, r.w, r.h);
        }
        renderer.endStripTarget();
        if (ready) renderer.writeGrayscalePlaneStrip(plane == GfxRenderer::GRAYSCALE_LSB, scratch.get(), y, rows);
      }
      if (!ready) break;
    }
    if (ready) renderer.displayGrayBuffer();
    renderer.setRenderMode(GfxRenderer::BW);
    if (!ready) renderer.cleanupGrayscaleWithFrameBuffer();
    homeGrayOnGlass = ready;
    if (!ready) homeGraySelected = false;
    LOG_INF("HOME", "tenor/ink gray covers=%d ready=%u ms=%lu internal_scratch=%u", found, ready ? 1u : 0u,
            static_cast<unsigned long>(millis() - started), static_cast<unsigned>(bytes));
    return ready;
  }
  const size_t bytes = renderer.getBufferSize();
  auto bw = HalMemory::allocatePsram(bytes);
  if (!bw) return false;
  std::memcpy(bw.get(), renderer.getFrameBuffer(), bytes);
  const uint32_t started = millis();
  if (!renderer.displayGrayscaleBase(HalDisplay::GrayscaleMode::Absolute,
                                    cleanInitialRefresh ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH)) return false;
  bool ready = true;
  for (const auto plane : {GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB}) {
    std::memcpy(renderer.getFrameBuffer(), bw.get(), bytes);  // identical BW UI in both absolute planes
    renderer.setRenderMode(plane);
    for (int i = 0; i < inkSlotCount; ++i) {
      if (!present[i]) continue;
      const auto& slot = inkSlots[i];
      const auto& r = slot.rect;
      auto file = Storage.open(paths[i].c_str());
      if (!file) { ready = false; break; }
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() != BmpReaderError::Ok || !renderer.drawBitmap(bitmap, r.x, r.y, r.w, r.h)) {
        ready = false; break;
      }
      homeCoverCorners(renderer, r.x, r.y, r.w, r.h);
    }
    if (!ready) break;
    if (plane == GfxRenderer::GRAYSCALE_LSB) renderer.copyGrayscaleLsbBuffers();
    else renderer.copyGrayscaleMsbBuffers();
  }
  if (ready) renderer.displayGrayBuffer();
  std::memcpy(renderer.getFrameBuffer(), bw.get(), bytes);
  renderer.setRenderMode(GfxRenderer::BW);
  if (!ready) renderer.cleanupGrayscaleWithFrameBuffer();
  homeGrayOnGlass = ready;
  LOG_INF("HOME", "tenor/ink gray covers=%d ready=%u ms=%lu psram_scratch=%u", found, ready ? 1u : 0u,
          static_cast<unsigned long>(millis() - started), static_cast<unsigned>(bytes));
  return ready;
}

std::string HomeActivity::holderPath(const int width, const int height) const {
  return homeInkEnabled() ? touchCoverPath(coverholder::path(), width, height)
                          : tenorink::cachePath(coverholder::path(), width, height, homeGraySelected);
}

// The one rule for the placeholder (founder 11/10/2026): a book with no cover of its own, or one Home cannot
// write (not an EPUB, a picture the decoders do not read). A book whose cover is still being written keeps
// the plain frame, so the placeholder never comes up between 2 frames of a book that has a cover.
bool HomeActivity::coverless(const int index) const {
  const auto& book = recentBooks[index];
  if (book.coverBmpPath.empty() || !FsHelpers::hasEpubExtension(book.path)) return true;
  std::string href;
  // An empty path in the reader's cover.ref is a book without a cover; no cover.ref, the book is asked.
  return coverref::load(book.coverBmpPath.substr(0, book.coverBmpPath.rfind('/')), href) &&
         !FsHelpers::hasJpgExtension(href) && !FsHelpers::hasPngExtension(href);
}

// A cover frame with nothing of the book's to draw. The placeholder (components/CoverHolder.h) for a book
// with no cover: the cached picture when the card has it, else a plain frame while an idle pass writes the
// picture. Any other book: the plain frame. True when the picture was drawn.
bool HomeActivity::drawHolder(const int book, const bool holder, const int x, const int y, const int width,
                              const int height) {
  if (!holder) {
    renderer.fillRoundedRect(x, y, width, height, tenorradius::cover(width), Color::LightGray);
    return false;
  }
  LOG_INF("HOME", "Cover holder frame=%d,%d,%d,%d", x, y, width, height);
  if (!homeGraySelected) {
    drawNameHolder(book, x, y, width, height);
    return true;
  }
  HalFile file;
  if (Storage.openFileForRead("HOME", holderPath(width, height), file)) {
    Bitmap bitmap(file);
    // Written at the frame's size, so it is drawn 1:1 as a book's own ink cover is.
    if (bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() == width && bitmap.getHeight() == height &&
        renderer.drawBitmap(bitmap, x, y, width, height))
      return true;
  }
  wantHolder(width, height);
  renderer.fillRoundedRect(x, y, width, height, tenorradius::cover(width), Color::LightGray);
  return false;
}

// The placeholder of a 1 bit frame, made on the spot from the book's own name: a white cover inside a 2 px dotted
// ring of gray (the plain frame's gray, as the page has no black rule round a cover; 2 px because a 1 px line of
// the 4 x 4 dither vanishes on odd columns and rows), the title in bold up to 3 lines with
// an ellipsis, the author under it. Faces: UI 12 over UI 10 on a frame 200 px wide or more (a card's cover), UI 10
// over the shelf's small face on a narrower one (the shelf). Both have the SD fallback a CJK title needs.
// Each line is centred on its ink and the block is centred in the frame. Margins: 1.5 corner radii.
void HomeActivity::drawNameHolder(const int book, const int x, const int y, const int width, const int height) {
  const int radius = tenorradius::cover(width);
  renderer.fillRoundedRect(x, y, width, height, radius, Color::LightGray);
  renderer.fillRoundedRect(x + 2, y + 2, width - 4, height - 4, tenorradius::nest(radius, 2), Color::White);
  const auto& entry = recentBooks[book];
  const auto title = entry.title.empty() ? entry.path.substr(entry.path.find_last_of('/') + 1) : entry.title;
  const bool narrow = width < 200;
  const int titleFont = narrow ? UI_10_FONT_ID : UI_12_FONT_ID;
  const int authorFont = narrow ? SMALL_FONT_ID : UI_10_FONT_ID;
  const int room = width - 2 * (radius * 3 / 2);
  const auto lines = renderer.wrappedText(titleFont, title.c_str(), room, 3, EpdFontFamily::BOLD);
  const auto author = entry.author.empty() ? std::string() : renderer.truncatedText(authorFont, entry.author.c_str(), room);
  const int titleHeight = renderer.getLineHeight(titleFont);
  const int gap = author.empty() ? 0 : radius / 2;
  const int block = static_cast<int>(lines.size()) * titleHeight + (author.empty() ? 0 : gap + renderer.getLineHeight(authorFont));
  int lineY = y + (height - block) / 2;
  const auto centred = [&](const int font, const int top, const std::string& text, const EpdFontFamily::Style style) {
    const auto ink = renderer.getTextInkBounds(font, text.c_str(), style);
    renderer.drawText(font, x + (width - (ink[2] - ink[0])) / 2 - ink[0], top, text.c_str(), true, style);
  };
  for (const auto& line : lines) {
    centred(titleFont, lineY, line, EpdFontFamily::BOLD);
    lineY += titleHeight;
  }
  if (!author.empty()) centred(authorFont, lineY + gap, author, EpdFontFamily::REGULAR);
}

void HomeActivity::wantHolder(const int width, const int height) {
  const uint32_t id = holderId(width, height, homeGraySelected);
  for (const uint32_t tried : holderTried)
    if (tried == id) return;
  const int count = holderCount.load();
  for (int i = 0; i < count; ++i)
    if (holderRequests[i].width == width && holderRequests[i].height == height) return;
  if (count >= static_cast<int>(holderRequests.size())) return;
  holderRequests[count] = {static_cast<int16_t>(width), static_cast<int16_t>(height)};
  holderAtMs = millis();
  holderCount = count + 1;
}

// Main task, once the card has been idle: writes the placeholders the frames asked for. True when one was
// written and the card asked to be drawn again.
bool HomeActivity::tickHolders() {
  if (holderCount.load() == 0) return false;
  if (mappedInput.wasAnyPressed() || mappedInput.wasAnyReleased()) {
    holderAtMs = millis();
    return false;
  }
  if (millis() - holderAtMs.load() < HOLDER_IDLE_MS) return false;
  RenderLock lock(*this);
  HalPowerManager::Lock fullSpeed;
  const auto requests = holderRequests;
  const int count = holderCount.exchange(0);
  if (activeTabId != Tab::RECENT) return false;
  // A card over a plain frame keeps no file, so its snapshot is still held: it goes before the writes, as a
  // card on file has let it go after its frame, and the card is drawn again from the files.
  freeCoverBuffer();
  coverRendered = false;
  bool wrote = false;
  for (int i = 0; i < count; ++i) {
    const int width = requests[i].width, height = requests[i].height;
    holderTried[holderTriedNext++ % 8] = holderId(width, height, homeGraySelected);
    const uint32_t started = millis();
    const bool ok = writeHolder(width, height);
    LOG_INF("HOME", "Cover holder %dx%d ms=%lu ok=%u free=%u largest=%u", width, height,
            static_cast<unsigned long>(millis() - started), ok ? 1u : 0u, static_cast<unsigned>(ESP.getFreeHeap()),
            static_cast<unsigned>(ESP.getMaxAllocHeap()));
    wrote = wrote || ok;
  }
  if (!wrote) return false;
  requestUpdate();
  return true;
}

// Written under a temporary name and renamed once whole, as every cover thumbnail is.
bool HomeActivity::writeHolder(const int width, const int height) {
  const std::string path = holderPath(width, height);
  if (!Storage.ensureDirectoryExists(coverholder::dir().c_str())) return false;
  const std::string part = path + ".tmp";
  Storage.remove(part.c_str());
  HalFile file;
  bool ok = Storage.openFileForWrite("HOME", part, file);
  if (ok) ok = coverholder::writeInk(file, width, height, homeGraySelected);
  file.close();
  if (ok) {
    Storage.remove(path.c_str());
    ok = Storage.rename(part.c_str(), path.c_str());
  }
  if (!ok) Storage.remove(part.c_str());
  return ok;
}

void HomeActivity::drawTouchCover(const HomeRecentRect& rect, const int index) {
  const auto& book = recentBooks[index];
  bool image = false;
  if (!book.coverBmpPath.empty()) {
    HalFile file;
    const auto path = touchCoverPath(book.coverBmpPath, rect.w, rect.h);
    if (Storage.openFileForRead("HOME", path, file)) {
      Bitmap bitmap(file);
      image = bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() == rect.w &&
              bitmap.getHeight() == rect.h && renderer.drawBitmap(bitmap, rect.x, rect.y, rect.w, rect.h);
    }
  }
  if (!image) {
    const bool holder = coverless(index);
    if (!holder) queueTouchThumb(index, rect.w, rect.h);
    drawHolder(index, holder, rect.x, rect.y, rect.w, rect.h);
  } else if (tenorchrome::kTouchShell && homeInkEnabled() && !homeGraySelected &&
             !Storage.exists(tenorink::cachePath(book.coverBmpPath, rect.w, rect.h, false).c_str())) {
    // The four-level stand-in loses its grays in one bit: make the dithered one-bit cover.
    queueTouchThumb(index, rect.w, rect.h);
  }
  homeCoverCorners(renderer, rect.x, rect.y, rect.w, rect.h);
  LOG_INF("HOME", "Home cover book=%d rect=%d,%d,%d,%d native=%u", index, rect.x, rect.y, rect.w, rect.h,
          image ? 1u : 0u);
}

void HomeActivity::drawTouchRecent(const HomeRecentLayout& f) {
  if (recentBooks.empty()) {
    renderer.drawText(UI_12_FONT_ID, 24, coverTileTop() + 26, tr(STR_NO_RECENT_BOOKS));
    return;
  }
  const uint32_t started = micros();
  const int shown = shownRecent();
  if (homeInkEnabled()) {
    inkSlotCount = 1;
    inkSlots[0] = {f.cover, shown};
    for (int slot = 0; slot < f.shelfCount; ++slot) {
      const int book = f.shelfBooks[slot];
      const auto size = cardCoverSize(book);
      const auto fit = homeCoverFit(f.shelf[slot].w, f.shelf[slot].h, size.width, size.height);
      const auto& cell = f.shelf[slot];
      inkSlots[inkSlotCount++] = {{24 + slot * 152 + (128 - fit.first) / 2,
                                 cell.y + (cell.h - fit.second) / 2, fit.first, fit.second}, book};
    }
  }
  heldCover = {static_cast<int16_t>(f.cover.x), static_cast<int16_t>(f.cover.y),
                static_cast<int16_t>(f.cover.w), static_cast<int16_t>(f.cover.h)};
  if (coverBufferStored && coverBufferBook == shown && restoreCoverBuffer()) {
    LOG_INF("HOME", "Home style=%u restore_us=%lu snapshot=%u", static_cast<unsigned>(f.style),
            static_cast<unsigned long>(micros() - started), static_cast<unsigned>(coverBufferSize));
    if (f.style != HomeRecentStyle::Shelf && recentBooks.size() > 1) drawOtherBookArrows(shown, f.rowY);
    return;
  }
  freeCoverBuffer();
  touchThumbCount = 0;
  LOG_INF("HOME", "Home build start style=%u", static_cast<unsigned>(f.style));
  LOG_INF("HOME", "Home layout style=%u title=%d author=%d progress=%d excerpt=%d rule=%d shelf=%d", static_cast<unsigned>(f.style),
          f.titleY, f.authorY, f.progressY, f.excerptY, f.ruleY, f.shelfHeadingY);
  loadCardStats(shown);
  drawTouchCover(f.cover, shown);
  const auto& book = recentBooks[shown];
  const auto title = book.title.empty() ? book.path.substr(book.path.find_last_of('/') + 1) : book.title;
  const int titleFont = homeExcerptUsesUiFont(title.c_str()) ? UI_12_FONT_ID : UI_TITLE_FONT_ID;
  const bool centered = f.style == HomeRecentStyle::LargeCover;
  auto line = [&](int font, int x, int y, int width, const std::string& text, EpdFontFamily::Style style) {
    if (centered) {
      const auto ink = renderer.getTextInkBounds(font, text.c_str(), style);
      const int left = (renderer.getScreenWidth() - (ink[2] - ink[0])) / 2 - ink[0];
      renderer.drawText(font, left, y, text.c_str(), true, style);
    }
    else renderer.drawText(font, x, y, renderer.truncatedText(font, text.c_str(), width, style).c_str(), true, style);
  };
  int y = f.titleY;
  for (const auto& text : renderer.wrappedText(titleFont, title.c_str(), f.text.w, f.titleLines, EpdFontFamily::BOLD)) {
    line(titleFont, f.text.x, y, f.text.w, text, EpdFontFamily::BOLD);
    y += renderer.getLineHeight(titleFont);
  }
  y = f.authorY;
  auto author = book.author.empty() ? std::string(tr(STR_RECENT_NO_AUTHOR)) : book.author;
  const auto comma = author.find(", ");
  if (f.authorLines > 1 && comma != std::string::npos) {
    line(UI_10_FONT_ID, f.text.x, y, f.text.w, author.substr(0, comma), EpdFontFamily::REGULAR);
    line(UI_10_FONT_ID, f.text.x, y + renderer.getLineHeight(UI_10_FONT_ID), f.text.w,
         author.substr(comma + 2), EpdFontFamily::REGULAR);
  } else {
    for (const auto& text : renderer.wrappedText(UI_10_FONT_ID, author.c_str(), f.text.w, f.authorLines)) {
      line(UI_10_FONT_ID, f.text.x, y, f.text.w, text, EpdFontFamily::REGULAR);
      y += renderer.getLineHeight(UI_10_FONT_ID);
    }
  }
  char progress[128];
  if (cardStats.recorded && !centered) snprintf(progress, sizeof(progress), "%u%%", static_cast<unsigned>(cardStats.percent));
  else if (cardStats.recorded) snprintf(progress, sizeof(progress), tr(STR_HOME_PROGRESS), static_cast<unsigned>(cardStats.percent));
  else snprintf(progress, sizeof(progress), "%s", tr(STR_STATS_NOT_RECORDED));
  if (cardStats.rows & (1u << HOME_STAT_FINISH)) {
    const size_t used = strlen(progress);
    snprintf(progress + used, sizeof(progress) - used, tr(STR_HOME_FINISH), cardStats.values[HOME_STAT_FINISH].c_str());
  }
  const int progressX = centered ? 96 : f.text.x;
  const int progressW = centered ? renderer.getScreenWidth() - 192 : f.text.w;
  auto bar = [&](int x, int y, int w, int h, int percent) {
    renderer.drawRoundedRect(x, y, w, h, 1, h / 2, true);
    const int fill = (w - 4) * percent / 100;
    if (fill > 0) renderer.fillRoundedRect(x + 2, y + 2, fill, h - 4, (h - 4) / 2, Color::Black);
  };
  bar(progressX, f.progressY, progressW, 10, cardStats.percent);
  const int progressFont = renderer.getTextWidth(UI_10_FONT_ID, progress) > f.text.w ? SMALL_FONT_ID : UI_10_FONT_ID;
  const auto fitted = renderer.truncatedText(progressFont, progress, f.text.w);
  line(progressFont, f.text.x, f.progressY + 22, f.text.w, fitted, EpdFontFamily::REGULAR);
  if (f.style == HomeRecentStyle::Shelf) {
    bool quoted = false;
    const auto excerpt = cardExcerpt(shown, quoted);
    const std::string quote = excerpt.empty() ? std::string(tr(STR_RECENT_NO_EXCERPT))
                              : quoted ? std::string("“") + excerpt + "”" : excerpt;
    const bool cjk = homeExcerptUsesUiFont(quote.c_str());
    const int serif = SETTINGS.uiTextSize == 1 ? NOTOSERIF_14_FONT_ID
                      : SETTINGS.uiTextSize == 2 ? NOTOSERIF_16_FONT_ID : NOTOSERIF_12_FONT_ID;
    const int font = excerpt.empty() ? SMALL_FONT_ID : cjk ? UI_12_FONT_ID : serif;
    const auto style = excerpt.empty() || cjk ? EpdFontFamily::REGULAR : EpdFontFamily::ITALIC;
    renderer.drawText(font, 24, f.excerptY, renderer.truncatedText(font, quote.c_str(), renderer.getScreenWidth() - 48, style).c_str(), true, style);
    if (f.shelfCount) renderer.drawText(UI_10_FONT_ID, 24, f.shelfHeadingY, tr(STR_RECENT_OTHER_BOOK));
    for (int slot = 0; slot < f.shelfCount; ++slot) {
      const int index = f.shelfBooks[slot];
      const auto& other = recentBooks[index];
      const auto size = cardCoverSize(index);
      const auto fittedCover = homeCoverFit(f.shelf[slot].w, f.shelf[slot].h, size.width, size.height);
      const auto& cell = f.shelf[slot];
      const int x = 24 + slot * 152;
      drawTouchCover({x + (128 - fittedCover.first) / 2, cell.y + (cell.h - fittedCover.second) / 2,
                      fittedCover.first, fittedCover.second}, index);
      loadCardStats(index);
      const int bottom = cell.y + cell.h;
      bar(x, bottom + 8, 128, 6, cardStats.percent);
      const auto name = other.title.empty() ? other.path.substr(other.path.find_last_of('/') + 1) : other.title;
      renderer.drawText(SMALL_FONT_ID, x, bottom + 20, renderer.truncatedText(SMALL_FONT_ID, name.c_str(), 128).c_str());
      snprintf(progress, sizeof(progress), tr(STR_HOME_PROGRESS), static_cast<unsigned>(cardStats.percent));
      if (renderer.getTextWidth(SMALL_FONT_ID, progress) > 128) snprintf(progress, sizeof(progress), "%u%%", static_cast<unsigned>(cardStats.percent));
      renderer.drawText(SMALL_FONT_ID, x, bottom + 20 + renderer.getLineHeight(SMALL_FONT_ID), progress);
    }
  } else if (recentBooks.size() > 1) {
    const auto& other = recentBooks[(shown + 1) % recentBooks.size()];
    const auto name = other.title.empty() ? other.path.substr(other.path.find_last_of('/') + 1) : other.title;
    const int room = renderer.getScreenWidth() - 80 - renderer.getTextWidth(UI_10_FONT_ID, tr(STR_RECENT_OTHER_BOOK)) - 16;
    drawOtherBookRow(shown, f.ruleY, f.rowY, renderer.wrappedText(UI_10_FONT_ID, name.c_str(), room, 1, EpdFontFamily::BOLD));
  }
  coverRectX = f.snapshot.x; coverRectY = f.snapshot.y;
  coverRectW = f.snapshot.w; coverRectH = f.snapshot.h;
  textRectW = textRectH = 0;
  coverBufferStored = storeCoverBuffer();
  coverBufferBook = shown;
  coverRendered = true;
  if (auto* cache = renderer.getFontCacheManager()) cache->releaseBuiltinPageCaches();
  LOG_INF("HOME", "Home style=%u build_us=%lu snapshot=%u thumbs=%d free=%u largest=%u", static_cast<unsigned>(f.style),
          static_cast<unsigned long>(micros() - started), static_cast<unsigned>(coverBufferSize), touchThumbCount,
          ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  LOG_INF("HOME", "Home build end style=%u", static_cast<unsigned>(f.style));
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
// book to step to. Drawn on every paint in up to two lines of an uncompressed flash font. The arrows
// sit outside the text margins as mirror images, so each title line ends on the right margin as the
// label starts on the left one.
void HomeActivity::drawOtherBookRow(const int shown, const int ruleY, const int rowY,
                                  const std::vector<std::string>& titleLines) {
  const int count = static_cast<int>(recentBooks.size());
  if (count < 2) return;
  // The arrows' tips stay where the solid triangles had them, 24 px outside the text margins.
  constexpr int MARGIN = 40;
  const int left = MARGIN, right = renderer.getScreenWidth() - MARGIN;
  renderer.drawLine(left, ruleY, right - 1, ruleY);
  const char* label = tr(STR_RECENT_OTHER_BOOK);
  rememberContextLine(UI_10_FONT_ID, left, rowY, label);
  renderer.drawText(UI_10_FONT_ID, left, rowY, label);
  int y = rowY;
  for (const auto& line : titleLines) {
    const int titleWidth = renderer.getTextWidth(UI_10_FONT_ID, line.c_str(), EpdFontFamily::BOLD);
    rememberContextLine(UI_10_FONT_ID, right - titleWidth, y, line.c_str(), EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, right - titleWidth, y, line.c_str(), true, EpdFontFamily::BOLD);
    y += renderer.getLineHeight(UI_10_FONT_ID);
  }
  drawOtherBookArrows(shown, rowY);
}

void HomeActivity::drawOtherBookArrows(const int shown, const int rowY) {
  constexpr int MARGIN = 40, ARROW_SPAN = 7, ARROW_OUT = 24;
  const int left = MARGIN, right = renderer.getScreenWidth() - MARGIN;
  const int top = rowY + renderer.getFontAscenderSize(UI_10_FONT_ID) * 2 / 3 - ARROW_SPAN;
  const int length = tenorchrome::moreChevronLength(ARROW_SPAN);
  tenorchrome::drawMoreChevron(renderer, right + ARROW_OUT - length, top, tenorchrome::ChevronDir::Right, ARROW_SPAN);
  // Right wraps to the most recent book from the last one, so only the left arrow can be missing.
  if (shown > 0)
    tenorchrome::drawMoreChevron(renderer, left - ARROW_OUT, top, tenorchrome::ChevronDir::Left, ARROW_SPAN);
}
