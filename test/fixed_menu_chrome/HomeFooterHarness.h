#include <optional>
enum class StrId {
  STR_CLEAR_CACHE_WARNING_1,
  STR_CLEAR_CACHE_WARNING_2,
  STR_CLEAR_CACHE_WARNING_3,
  STR_CLEAR_CACHE_WARNING_4,
  STR_BACK,
  STR_SELECT,
  STR_DIR_LEFT,
  STR_DIR_RIGHT,
  STR_DIR_UP,
  STR_DIR_DOWN,
  STR_FOLDER_HOLD,
  STR_HOME_FAVORITES_HINT,
  STR_FILE_NOT_FOUND,
  STR_FILE_NOT_FOUND_TOUCH,
  STR_FILE_HOLD_ACTIONS,
  STR_REMOVE_FROM_RECENTS_FAILED,
  STR_MENU_SAVE_FAILED,
  STR_STATS_RESET_FAILED,
  STR_STATS_RESET_PENDING,
  STR_HABIT_SAVE_FAILED,
  STR_CALIBRE_URL_HINT,
  STR_ERROR_GENERAL_FAILURE,
  STR_CLEARING_CACHE,
  STR_CLEAR_CACHE_FAILED,
  STR_CHECK_SERIAL_OUTPUT,
  STR_CACHE_CLEARED,
  STR_ITEMS_REMOVED,
  STR_FAILED_LOWER,
  STR_CANCEL,
  STR_CLEAR_BUTTON,
  STR_CLEAR_READING_CACHE,
  STR_LOADING_FONT_LIST,
  STR_FONT_INSTALLED,
  STR_FONT_INSTALL_FAILED,
  STR_FONT_BATCH_DONE,
  STR_FONT_BATCH_FAILED,
  STR_FONT_BATCH_DOWNLOADING,
  STR_FONT_BATCH_TOTAL,
  STR_FONT_PACK_INSTALLING,
  STR_DOWNLOADING,
  STR_FONT_BROWSER,
  STR_RETRY,
  STR_FONT,
  STR_SIZE,
  STR_LINE_SPACING,
  STR_FOCUS_READING,
  STR_HYPHENATION,
  STR_FAVORITES_EMPTY,
  STR_READER_FAVORITES_EMPTY,
  STR_INK_UNAVAILABLE,
  STR_NOT_IN_PREVIEW,
  STR_FILE_SIDE_ACTIONS,
  STR_FOLDER_TOO_MANY,
  STR_NO_BIN_FILES,
  STR_NO_FILES_FOUND,
  STR_NO_BOOKMARKS,
  STR_HOLD_OPEN_FOR_ACTIONS,
  STR_CHAPTER_NUMBER_HINT,
  STR_CHAPTER_SEARCHING,
  STR_CHAPTER_NOT_FOUND,
  STR_NO_CHAPTERS,
  STR_FAVORITES_HINT,
  STR_CHAPTER_PREFIX,
  STR_PAGES_SEPARATOR,
  STR_BOOK_PREFIX,
  STR_INDEXING,
  STR_QUOTES_TRIM_HELP_MOVE,
  STR_QUOTES_TRIM_HELP_SWITCH,
  STR_QUOTES_TRIM_HELP_BACK,
  STR_QUOTES_TRIM_KEEP,
  STR_QUOTES_TRIM_END,
  STR_QUOTES_TRIM_START,
  STR_QUOTES_COUNT_LIBRARY,
  STR_QUOTES_COUNT_ONE_PAGE,
  STR_QUOTES_PAGE_OF,
  STR_QUOTES_UNREADABLE,
  STR_QUOTES_EMPTY,
};
// PHASE2_TRANSLATIONS
constexpr auto STR_BACK = StrId::STR_BACK;
constexpr auto STR_SELECT = StrId::STR_SELECT;
constexpr auto STR_DIR_LEFT = StrId::STR_DIR_LEFT;
constexpr auto STR_DIR_RIGHT = StrId::STR_DIR_RIGHT;
constexpr auto STR_DIR_UP = StrId::STR_DIR_UP;
constexpr auto STR_DIR_DOWN = StrId::STR_DIR_DOWN;
constexpr auto STR_FOLDER_HOLD = StrId::STR_FOLDER_HOLD;
constexpr auto STR_HOME_FAVORITES_HINT = StrId::STR_HOME_FAVORITES_HINT;
constexpr auto STR_FILE_NOT_FOUND = StrId::STR_FILE_NOT_FOUND;
constexpr auto STR_FILE_NOT_FOUND_TOUCH = StrId::STR_FILE_NOT_FOUND_TOUCH;
constexpr auto STR_FILE_HOLD_ACTIONS = StrId::STR_FILE_HOLD_ACTIONS;
constexpr auto STR_REMOVE_FROM_RECENTS_FAILED = StrId::STR_REMOVE_FROM_RECENTS_FAILED;
constexpr auto STR_MENU_SAVE_FAILED = StrId::STR_MENU_SAVE_FAILED;
constexpr auto STR_HABIT_SAVE_FAILED = StrId::STR_HABIT_SAVE_FAILED;
constexpr auto STR_CALIBRE_URL_HINT = StrId::STR_CALIBRE_URL_HINT;
constexpr auto STR_ERROR_GENERAL_FAILURE = StrId::STR_ERROR_GENERAL_FAILURE;
#define FIXTURE_STRING(name) constexpr auto name = StrId::name;
FIXTURE_STRING(STR_CLEAR_CACHE_WARNING_1)
FIXTURE_STRING(STR_CLEAR_CACHE_WARNING_2)
FIXTURE_STRING(STR_CLEAR_CACHE_WARNING_3)
FIXTURE_STRING(STR_CLEAR_CACHE_WARNING_4)
FIXTURE_STRING(STR_CLEARING_CACHE)
FIXTURE_STRING(STR_CLEAR_CACHE_FAILED)
FIXTURE_STRING(STR_CHECK_SERIAL_OUTPUT)
FIXTURE_STRING(STR_CACHE_CLEARED)
FIXTURE_STRING(STR_ITEMS_REMOVED)
FIXTURE_STRING(STR_FAILED_LOWER)
FIXTURE_STRING(STR_CANCEL)
FIXTURE_STRING(STR_CLEAR_BUTTON)
FIXTURE_STRING(STR_CLEAR_READING_CACHE)
FIXTURE_STRING(STR_LOADING_FONT_LIST)
FIXTURE_STRING(STR_FONT_INSTALLED)
FIXTURE_STRING(STR_FONT_INSTALL_FAILED)
FIXTURE_STRING(STR_FONT_BATCH_DONE)
FIXTURE_STRING(STR_FONT_BATCH_FAILED)
FIXTURE_STRING(STR_FONT_BATCH_DOWNLOADING)
FIXTURE_STRING(STR_FONT_BATCH_TOTAL)
FIXTURE_STRING(STR_FONT_PACK_INSTALLING)
FIXTURE_STRING(STR_DOWNLOADING)
FIXTURE_STRING(STR_FONT_BROWSER)
FIXTURE_STRING(STR_RETRY)
FIXTURE_STRING(STR_FAVORITES_EMPTY)
FIXTURE_STRING(STR_READER_FAVORITES_EMPTY)
FIXTURE_STRING(STR_INK_UNAVAILABLE)
FIXTURE_STRING(STR_NOT_IN_PREVIEW)
#undef FIXTURE_STRING
const char* tr(StrId id) {
  // PHASE2_LOOKUP
  switch (id) {
    case StrId::STR_FILE_NOT_FOUND: return "File not found";
    case StrId::STR_FILE_NOT_FOUND_TOUCH: return "File not found, touch and hold to unpin";
    case StrId::STR_FILE_HOLD_ACTIONS: return "Hold OK: Pin, rename, delete";
    case StrId::STR_REMOVE_FROM_RECENTS_FAILED: return "Could not remove book";
    case StrId::STR_STATS_RESET_FAILED: return "Stats reset failed";
    case StrId::STR_STATS_RESET_PENDING: return "Stats reset pending";
    case StrId::STR_MENU_SAVE_FAILED: return "Menu save failed";
    case StrId::STR_HOME_FAVORITES_HINT: return "Hold OK to pin\nHold keys to reorder";
    case StrId::STR_FOLDER_HOLD: return "Hold OK to open options";
    case StrId::STR_FONT: return "Font";
    case StrId::STR_SIZE: return "Font size";
    case StrId::STR_LINE_SPACING: return "Line spacing";
    case StrId::STR_RETRY: return "Retry";
    case StrId::STR_FOCUS_READING: return "Drop cap";
    case StrId::STR_HYPHENATION: return "Hyphenation";
    case StrId::STR_FAVORITES_EMPTY: return "No favorites yet";
    case StrId::STR_READER_FAVORITES_EMPTY: return "Hold a text row to pin it";
    case StrId::STR_INK_UNAVAILABLE: return "Ink weight unavailable";
    case StrId::STR_NOT_IN_PREVIEW: return "Not shown in preview";
    default: return "key";
  }
}
struct I18nBoundary { const char* get(StrId id) const { return tr(id); } } I18N;
struct FooterBoundary {
  int draws = 0;
  void drawButtonHints(const GfxRenderer&, const char*, const char*, const char*, const char*) { ++draws; }
  template <typename Box> void drawProgressBar(const GfxRenderer&, Box box, int, int) {
    progressBottom = box.y + box.height;
  }
  template <typename Box> void drawHeader(const GfxRenderer&, Box, const char*, const char*) {}
  int progressBottom = 0;
} GUI;
struct UiScreen;
struct UiListActivity {
  GfxRenderer& renderer;
  explicit UiListActivity(GfxRenderer& target) : renderer(target) {}
  bool favoriteSaveFailed = false, supported = false, tabBandDrawn = false;
  int favoriteHintY = -1;
  bool supportsFavorites() const { return supported; }
  bool ownsX3Chrome() const { return false; }
  int favoriteHintLinesAbove() const { return 0; }
  const char* favoriteHintText() {
    return favoriteSaveFailed ? tr(STR_MENU_SAVE_FAILED) : supported ? "Hold OK: Pin" : nullptr;
  }
  void reserveFavoriteHint(UiScreen& screen);
};
struct HomeActivity : UiListActivity {
  enum class Tab { RECENT, FOLDER, FAVORITES, STATS, CAI_DAT };
  struct InputBoundary {
    struct Labels { const char* btn1; const char* btn2; const char* btn3; const char* btn4; };
    Labels mapLabels(const char* back, const char* select, const char* left, const char* right) {
      return {back, select, left, right};
    }
  } mappedInput;
  GfxRenderer& renderer;
  explicit HomeActivity(GfxRenderer& target) : UiListActivity(target), renderer(target) { supported = true; }
  std::vector<int> recentBooks{1}, rowItems{1};
  Tab activeTabId = Tab::RECENT;
  bool favoriteFileMissing = false, favoriteSaveFailed = false;
  bool favoritesAvailable = true;
  std::optional<StrId> statsResetTip;
  bool recentRemovalFailed = false;
  int ring = 1;
  int ringPos() const { return ring; }
  bool supportsFavorites() const { return favoritesAvailable && activeTabId != Tab::STATS; }
  void drawFooter();
  const char* homeTipText();
  const char* homeInfoText();
  static const char* missingFileText();
  tenorchrome::X3ScreenLayout homeScreenLayout();
};

namespace freeink::ui {
struct Insets { int top, right, bottom, left; };
struct ListItem {
  const char* label = nullptr;
  int16_t actionValue = 0;
  bool opensNext = false;
};
enum class BitmapFormat { Mask1 };
struct BitmapRef {
  const uint8_t* data = nullptr;
  int width = 0, height = 0;
  BitmapFormat format = BitmapFormat::Mask1;
  bool progmem = false;
  explicit operator bool() const { return data != nullptr; }
};
inline BitmapRef bitmapFromIcon(const freeink::Icon& icon) { return {icon.bits}; }
}
namespace fui = freeink::ui;
struct UiScreen {
  tenorchrome::X3ScreenRect content{0, 0, 528, 792};
  void setContentMarginFromScreen(freeink::ui::Insets margin) {
    content = {margin.left, margin.top, 528 - margin.left - margin.right,
               792 - margin.top - margin.bottom};
  }
  tenorchrome::X3ScreenRect body() const { return content; }
  void takeBottom(int height) { content.height = std::max(0, content.height - height); }
  void takeTop(int height) { content.y += height; content.height = std::max(0, content.height - height); }
};

struct AtomicFlagBoundary {
  bool value = false;
  bool load() const { return value; }
};
struct SettingsScreenBoundary : UiListActivity {
  explicit SettingsScreenBoundary(GfxRenderer& target) : UiListActivity(target) {}
  AtomicFlagBoundary saveFailed, saveFailed_;
  bool showSaveError = false;
};
struct TextTabsBoundary {
  enum class Tab : uint8_t { Family, Size, Layout, Style, Favorites, Count };
  int displayIndex(Tab tab) const;
  Tab tabForIndex(int index) const;
};

using Rect = tenorchrome::X3ScreenRect;
struct ClearCacheActivity {
  enum State { WARNING, CLEARING, SUCCESS, FAILED };
  explicit ClearCacheActivity(GfxRenderer& target) : renderer(target) {}
  GfxRenderer& renderer;
  HomeActivity::InputBoundary mappedInput;
  State state = WARNING;
  int clearedCount = 17, failedCount = 3, infoPage_ = 0, infoPages_ = 1;
  struct { int selected() const { return 0; } } confirmPopup;
  std::string x3InfoText() const;
  void renderX3();
};
struct FontDownloadActivity {
  enum State { LOADING_MANIFEST, GROUP_LIST, FAMILY_LIST, DOWNLOADING, COMPLETE, ERROR };
  explicit FontDownloadActivity(GfxRenderer& target) : renderer(target) {}
  GfxRenderer& renderer;
  HomeActivity::InputBoundary mappedInput;
  State state_ = LOADING_MANIFEST;
  int infoPage_ = 0, infoPages_ = 1, downloadingFamilyIndex_ = 0;
  int currentFileIndex_ = 2, currentFileTotal_ = 4;
  size_t fileProgress_ = 4096, fileTotal_ = 8192;
  std::string errorMessage_ = "Network disconnected";
  bool batchResult_ = false, batchRunning_ = false, installingPack_ = false;
  unsigned batchSuccessCount_ = 0, batchFamilyCount_ = 1, batchFailureCount_ = 0, batchFamilyIndex_ = 1;
  int resultFailureIndex_ = 0;
  uint64_t familyDownloadedBytes_ = 0, batchTotalBytes_ = 8192, batchDownloadedBytes_ = 0;
  struct Family { std::string name = "Book font"; uint64_t totalSize = 8192; const char* failureReason = nullptr; unsigned requiredMb = 0; };
  std::vector<Family> families_{Family{}};
  const char* str(const std::string& text) const { return text.c_str(); }
  static std::string formatSize(size_t bytes) { return std::to_string(bytes); }
  const char* headerSubtitle() { return "Latin"; }
  std::string x3InfoText() const;
  void drawChrome();
  void renderX3State();
};

namespace shell { bool isUgly() { return false; } }
namespace menucustom {
struct State {
  int pinCount = 0;
  std::array<std::array<char, 64>, 32> pins{};
};
inline State& state() { static State pins; return pins; }
}
namespace menufavorites {
struct Descriptor { const char* key; StrId label; const char* screen; int tab, row; };
inline const Descriptor descriptors[] = {
  {"text/fontFamily", StrId::STR_FONT, "text", 0, 0},
  {"text/fontSize", StrId::STR_SIZE, "text", 1, 0},
  {"text/lineSpacing", StrId::STR_LINE_SPACING, "text", 2, 0},
  {"text/dropCapMode", StrId::STR_FOCUS_READING, "text", 3, 0},
  {"text/hyphenationEnabled", StrId::STR_HYPHENATION, "text", 3, 1},
  {"settings/clock", StrId::STR_FONT, "settings", 0, 0}
};
inline const Descriptor* find(const std::string& key) {
  for (const auto& route : descriptors) if (key == route.key) return &route;
  return nullptr;
}
inline StrId label(const std::string& key) { return find(key)->label; }
inline const char* keyFor(const char* screen, int tab, int row) {
  for (const auto& route : descriptors)
    if (std::strcmp(screen, route.screen) == 0 && tab == route.tab && row == route.row) return route.key;
  return "";
}
}
#include "components/SettingsChoiceStyle.h"
namespace settingstabs { bool moTrinhChon(int) { return false; } }
namespace readerInk { bool available(int level, int mask) { return (mask & (1 << level)) != 0; } }
struct { int mask = 15; int availableWeightMask() const { return mask; } } sdFontSystem;
struct TextSettingsActivity : UiListActivity {
  enum class Tab : uint8_t { Family, Size, Layout, Style, Favorites, Count };
  enum class LayoutRow { Count = 7 };
  enum class StyleRow { FocusReading, Hyphenation, EmbeddedStyle, AntiAliasing, InkWeight, Count };
  explicit TextSettingsActivity(GfxRenderer& target) : UiListActivity(target) { supported = true; }
  Tab tab_ = Tab::Family;
  std::array<uint8_t, 32> favoritePins_{};
  int favoriteCount_ = 0, ring = 1;
  int currentFamilyIndex_ = 0, currentSizeIndex_ = 0;
  int afterHeader = 0, bottomReserved = 0, usableHeight = 0, previewHeight = 0;
  struct { int previewHeightPercent = 35, verticalSpacing = 16; } metrics_;
  struct Entry { std::string name; };
  std::vector<Entry> fonts_{{"Font 1"}, {"Font 2"}}, sizes_{{"12"}, {"14"}};
  std::vector<std::string> rowValues_;
  std::vector<fui::ListItem> rowItems_;
  std::string screenInfo_;
  AtomicFlagBoundary saveFailed_;
  struct Navigation { int selected = 1; bool followOnBuild = false; } navigation;
  struct Input { bool hasTouch() const { return false; } } mappedInput;
  struct Popup { bool isActive() const { return false; } } optionPopup_;
  struct App { void clearTapFlash() {} } app;
  int activatedRow = -1;
  Tab activatedTab = Tab::Favorites;
  Navigation& activeNav() { return navigation; }
  void requestUpdate() {}
  void commitTabNavigation() {}
  void focusForm(int) {}
  static int formIndex(Tab, int row) { return row; }
  void applyChosenValue(Tab tab, int, int row) { activatedTab = tab; activatedRow = row; }
  void confirmLayoutRow(int row) { activatedTab = Tab::Layout; activatedRow = row; }
  void confirmStyleRow(int row) { activatedTab = Tab::Style; activatedRow = row; }
  int tabCount() const { return tenorchrome::x3ScreenLayoutEnabled() ? 5 : 4; }
  int activeTab() const { return displayIndex(tab_); }
  int displayIndex(Tab tab) const;
  Tab tabForIndex(int index) const;
  int ringPos() const { return ring; }
  static Tab formTab(int) { return Tab::Family; }
  static int formLocalRow(int row) { return row; }
  int settingsChoiceCount(int) const { return 0; }
  SettingsChoiceStyle rowChoiceStyle(int) const { return SettingsChoiceStyle::None; }
  int listCount() const;
  std::string favoriteKey(int row) const;
  void rebuildRowItems();
  void refreshFavoriteRows();
  void onTabAction(int index);
  int focusFavorite(const std::string& key);
  void activateRow(int row);
  const char* favoriteHintText();
  const char* x3TipText();
  std::string x3InfoText();
  bool focusedRowHasNoPreview() const;
  fui::BitmapRef tabIcon(int index, bool bold) const;
  void updatePreviewGeometry();
  void prepareBody(UiScreen& screen);
  void drawX3Info();
  void buildTabBar(UiScreen& screen) {
    const auto layout = tenorchrome::x3ScreenLayout(renderer);
    const int bottom = std::min(screen.body().bottom(), layout.bodyRect.bottom());
    screen.setContentMarginFromScreen({layout.bodyRect.y, 0, renderer.getScreenHeight() - bottom, 0});
    tabBandDrawn = true;
  }
};
struct RenderLock { template <typename Activity> explicit RenderLock(Activity&) {} };
inline constexpr StrId LAYOUT_ROW_NAME_IDS[7] = {};
inline constexpr StrId STYLE_ROW_NAME_IDS[5] = {};
