#include "HomeActivity.h"

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
#include "components/ReadingStatsView.h"
#include "components/TenorMenuChrome.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "components/X3SleepCover.h"
#include "components/icons/homeTabIcons.h"
#include "components/icons/tenorHomeTabIcons.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
constexpr StrId TAB_NAMES[HomeActivity::TAB_COUNT] = {StrId::STR_HOME_TAB_RECENT, StrId::STR_HOME_TAB_FOLDER,
                                                      StrId::STR_HOME_TAB_STATS, StrId::STR_SETTINGS_TITLE,
                                                      StrId::STR_READER_TAB_FAVORITES};
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
  RenderLock lock(*this);
  activeTabId = tab;
  favoriteFileMissing = false;
  // The tenor card stays fixed while browsing Home tabs. Reuse its existing
  // bitmap on wrap; onPause/onExit release it before opening another activity.
  if (tab != Tab::RECENT && !tenorchrome::enabled()) {
    freeCoverBuffer();
    coverRendered = false;
  }
  rebuildRows();
  if (!mappedInput.hasTouch() && listCount() > 0 && activeNav().selected <= 0) activeNav().selected = 1;
  activeNav().followOnBuild = true;
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
      for (size_t i = 0; i < recentBooks.size(); ++i)
        rowLabels.push_back(tenorchrome::enabled() && i == 0 ? tr(STR_CONTINUE_READING) : recentBooks[i].title);
      break;
    case Tab::FOLDER:
      docGocTheNho();
      for (const auto& muc : mucTheNho) rowLabels.push_back(muc);
      break;
    case Tab::STATS:
      rowLabels.emplace_back(tr(STR_READING_HABITS));
      rowLabels.emplace_back(tr(STR_STATS_BY_BOOK));
      rowLabels.emplace_back(tr(STR_STATS_MONTH));
      rowLabels.emplace_back(tr(STR_QUOTES));
      rowLabels.emplace_back(tr(STR_STATS_RESET_ALL));
      rowLabels.emplace_back(tr(STR_STATS_RESET_HABITS));
      break;
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

  if (activeTabId == Tab::FAVORITES) {
    const auto catalog = getSettingsList(&sdFontSystem.registry());
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
      const auto name = menufavorites::label(key, catalog);
      if (name == StrId::STR_NONE_OPT) continue;
      favoriteKeys.push_back(key);
      favoriteValues.push_back(menufavorites::value(key, catalog));
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
    case Tab::STATS:
      if (index == 0)
        startActivityForResult(makeUniqueNoThrow<ReadingHabitsActivity>(renderer, mappedInput), nullptr);
      else if (index == 1)
        startActivityForResult(makeUniqueNoThrow<BookStatsLibraryActivity>(renderer, mappedInput), nullptr);
      else if (index == 2)
        startActivityForResult(makeUniqueNoThrow<ReadingHistoryActivity>(renderer, mappedInput), nullptr);
      else if (index == 3)
        startActivityForResult(makeUniqueNoThrow<QuotesActivity>(renderer, mappedInput), nullptr);
      else
        confirmStatsReset(index == 4);
      return;
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
        RenderLock lock(*this);
        freeCoverBuffer();
        coverRendered = false;
      }
      startActivityForResult(ReaderActivity::create(renderer, mappedInput, path, false, true), nullptr);
    }
    return true;
  }
  // Back opens the most recently read book: it is otherwise unused here, and
  // recentBooks is most-recent-first and already pruned of missing files.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (!recentBooks.empty()) onSelectBook(recentBooks[0].path);
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (ringPos() == 0) {
      moveRingTo(1);  // step into the tab's first row; the edge buttons change tab
    } else {
      activateIndex(ringPos() - 1);
    }
    return true;
  }

  return false;
}

void HomeActivity::confirmStatsReset(const bool all) {
  statsResetFailed = false;
  startActivityForResult(
      makeUniqueNoThrow<ConfirmationActivity>(
          renderer, mappedInput, I18N.get(all ? StrId::STR_STATS_RESET_ALL : StrId::STR_STATS_RESET_HABITS),
          I18N.get(all ? StrId::STR_STATS_RESET_ALL_BODY : StrId::STR_STATS_RESET_HABITS_BODY)),
      [this, all](const ActivityResult& result) {
        if (result.isCancelled) return;
        RenderLock lock(*this);
        statsResetFailed = !READING_STATS.resetStatistics(all);
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
  UiListActivity::drawFooter();
  if (activeTabId == Tab::FOLDER && ringPos() == 0) tenorchrome::drawTip(renderer, tr(STR_FOLDER_HOLD));
  if (favoriteFileMissing) tenorchrome::drawTip(renderer, tr(STR_FILE_NOT_FOUND), 1, 3);
  if (activeTabId == Tab::STATS && statsResetFailed) tenorchrome::drawTip(renderer, tr(STR_STATS_RESET_FAILED), 1, 2);
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
    readingstatsview::draw(renderer, top + (suggestion ? readingstatsview::BADGE_HEIGHT : 0), false, suggestion);
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
             ? tenorchrome::TAB_TOP
             : metrics.topPadding + metrics.headerHeight;
}

int HomeActivity::preferredTabBarHeight() const {
  if (SETTINGS.uiTheme == CrossPointSettings::TENOR_UI && !mappedInput.hasTouch()) {
    // Include 2 px above, 4 below and the divider: the filled box equals a row.
    return tenorchrome::TAB_HEIGHT;
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
  // Leave the cover tile's band to drawChrome(); the list starts under it.
  if (activeTabId == Tab::RECENT) screen.takeTop(static_cast<int16_t>(recentCardHeight()));

  if (activeTabId == Tab::STATS) screen.takeTop(readingstatsview::HEIGHT + 12);

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
  if (activeTabId == Tab::STATS && statsResetFailed)
    screen.takeBottom(static_cast<int16_t>(28 + tenorchrome::tipHeight(renderer, tr(STR_STATS_RESET_FAILED), 2)));
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
  renderer.clearScreen();
  drawChrome();
  renderUi();
  drawFooter();
  renderer.displayBuffer(cleanInitialRefresh ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH);
  cleanInitialRefresh = false;
}

bool HomeActivity::storeCoverBuffer() {
  // drawChrome() must have already set the cover rect; without it we'd be back
  // to cloning the whole framebuffer.
  if (coverRectW <= 0 || coverRectH <= 0) return false;
  freeCoverBuffer();
  const size_t needed = renderer.getRegionByteSize(coverRectX, coverRectY, coverRectW, coverRectH);
  if (needed == 0) return false;
  coverBuffer = static_cast<uint8_t*>(malloc(needed));
  if (!coverBuffer) {
    LOG_ERR("HOME", "OOM: cover buffer (%u bytes)", (unsigned)needed);
    return false;
  }
  coverBufferSize = needed;
  if (!renderer.copyRegionToBuffer(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer, coverBufferSize)) {
    free(coverBuffer);
    coverBuffer = nullptr;
    coverBufferSize = 0;
    return false;
  }
  return true;
}

bool HomeActivity::restoreCoverBuffer() {
  if (!coverBuffer || coverRectW <= 0 || coverRectH <= 0) return false;
  return renderer.copyBufferToRegion(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer, coverBufferSize);
}

void HomeActivity::freeCoverBuffer() {
  if (coverBuffer) {
    free(coverBuffer);
    coverBuffer = nullptr;
  }
  coverBufferSize = 0;
  coverBufferStored = false;
}

void HomeActivity::loadRecentBooks() {
  recentBooks.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  // The store bounds this snapshot to ten books. Themes limit cover tiles separately.
  recentBooks.reserve(books.size());
  for (const RecentBook& book : books) {
    if (RecentBooksStore::isMissing(book)) continue;
    recentBooks.push_back(book);
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

int HomeActivity::recentCardHeight() const {
  return tenorchrome::enabled() ? (recentBooks.empty() ? 96 : 248)
                                : UITheme::getInstance().getMetrics().homeCoverTileHeight;
}
void HomeActivity::drawRecentCard() {
  const uint32_t started = millis();
  coverRectX = 0;
  coverRectY = coverTileTop();
  coverRectW = renderer.getScreenWidth();
  coverRectH = recentCardHeight();
  if (coverBufferStored && restoreCoverBuffer()) {
    LOG_DBG("HOME", "Recent card cached %lums", static_cast<unsigned long>(millis() - started));
    return;
  }
  const int top = coverRectY + 6;
  const int right = coverRectW - 24;
  if (recentBooks.empty()) {
    renderer.drawText(UI_12_FONT_ID, 24, top + 20, tr(STR_NO_RECENT_BOOKS));
    return;
  }
  const auto& book = recentBooks.front();
  renderer.drawText(UI_10_FONT_ID, 24, top, tr(STR_RECENT_LATEST));
  constexpr int coverX = 24, coverW = 124, coverH = 184;
  const int coverY = top + 34, textX = 168, textWidth = right - textX;
  bool image = false;
  // A failed allocation keeps navigation responsive using the cheap fallback.
  if (!coverRendered && !book.coverBmpPath.empty()) {
    const auto path =
        UITheme::getCoverThumbPath(book.coverBmpPath, UITheme::getInstance().getMetrics().homeCoverHeight);
    HalFile file;
    if (Storage.openFileForRead("HOME", path, file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() > 0 && bitmap.getHeight() > 0)
        image = renderer.drawBitmap(bitmap, coverX + 2, coverY + 2, coverW - 4, coverH - 4);
    }
  }
  if (!image) {
    // Already encoded for the menu. drawPixel applies the current orientation.
    for (int y = 0; y < sleepcover::HEIGHT; ++y)
      for (int x = 0; x < sleepcover::WIDTH; ++x) {
        const int bit = y * sleepcover::WIDTH + x;
        const bool white = (sleepcover::PIXELS[bit / 8] >> (7 - bit % 8)) & 1;
        renderer.drawPixel(coverX + 2 + x, coverY + 2 + y, !white);
      }
  }
  renderer.drawRect(coverX, coverY, coverW, coverH);
  int y = coverY;
  const auto title = book.title.empty() ? book.path.substr(book.path.find_last_of('/') + 1) : book.title;
  for (const auto& line : renderer.wrappedText(UI_12_FONT_ID, title.c_str(), textWidth, 2, EpdFontFamily::BOLD)) {
    renderer.drawText(UI_12_FONT_ID, textX, y, line.c_str(), true, EpdFontFamily::BOLD);
    y += 29;
  }
  y = coverY + 66;
  renderer.drawText(
      UI_10_FONT_ID, textX, y,
      renderer
          .truncatedText(UI_10_FONT_ID, book.author.empty() ? tr(STR_RECENT_NO_AUTHOR) : book.author.c_str(), textWidth)
          .c_str());
  y = coverY + 102;
  if (book.excerpt.empty()) {
    for (const auto& line : renderer.wrappedText(SMALL_FONT_ID, tr(STR_RECENT_NO_EXCERPT), textWidth, 2)) {
      renderer.drawText(SMALL_FONT_ID, textX, y, line.c_str());
      y += renderer.getLineHeight(SMALL_FONT_ID);
    }
  } else {
    const auto quote = std::string("“") + book.excerpt + "”";
    auto lines = renderer.wrappedText(NOTOSERIF_12_FONT_ID, quote.c_str(), textWidth, 3, EpdFontFamily::ITALIC);
    if (!lines.empty()) {
      auto& last = lines.back();
      const std::string closing = "”";
      if (last.size() < closing.size() || last.compare(last.size() - closing.size(), closing.size(), closing) != 0)
        last = renderer.truncatedText(
                   NOTOSERIF_12_FONT_ID, last.c_str(),
                   textWidth - renderer.getTextWidth(NOTOSERIF_12_FONT_ID, closing.c_str(), EpdFontFamily::ITALIC) - 2,
                   EpdFontFamily::ITALIC) +
               closing;
    }
    for (const auto& line : lines) {
      renderer.drawText(NOTOSERIF_12_FONT_ID, textX, y, line.c_str(), true, EpdFontFamily::ITALIC);
      y += renderer.getLineHeight(NOTOSERIF_12_FONT_ID);
    }
  }
  coverBufferStored = storeCoverBuffer();
  coverRendered = true;
  LOG_INF("HOME", "Recent card build=%lums cache=%u", static_cast<unsigned long>(millis() - started),
          static_cast<unsigned>(coverBufferSize));
}
