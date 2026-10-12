#include <optional>
enum class StrId {
  STR_BACK, STR_SELECT, STR_DIR_LEFT, STR_DIR_RIGHT, STR_DIR_UP, STR_DIR_DOWN,
  STR_FOLDER_HOLD, STR_HOME_FAVORITES_HINT, STR_FILE_NOT_FOUND, STR_FILE_NOT_FOUND_TOUCH, STR_FILE_HOLD_ACTIONS, STR_MENU_SAVE_FAILED,
  STR_STATS_RESET_FAILED, STR_STATS_RESET_PENDING, STR_REMOVE_FROM_RECENTS_FAILED
};
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
const char* tr(StrId id) {
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
    default: return "key";
  }
}
struct I18nBoundary { const char* get(StrId id) const { return tr(id); } } I18N;
struct FooterBoundary {
  int draws = 0;
  void drawButtonHints(const GfxRenderer&, const char*, const char*, const char*, const char*) { ++draws; }
} GUI;
struct UiListActivity {
  const char* favoriteHintText() { return "Hold OK: Pin"; }
};
struct HomeActivity : UiListActivity {
  enum class Tab { RECENT, FOLDER, FAVORITES, STATS, CAI_DAT };
  struct InputBoundary {
    enum class Button { Back, Confirm, Left, Right, Up, Down, None };
    Button released = Button::None;
    bool wasReleased(Button button) const { return released == button; }
    bool wasAnyPressed() const { return false; }
    bool wasAnyReleased() const { return released != Button::None; }
    bool wasScreenTouchReleased() const { return false; }
    struct Labels { const char* btn1; const char* btn2; const char* btn3; const char* btn4; };
    Labels mapLabels(const char* back, const char* select, const char* left, const char* right) {
      return {back, select, left, right};
    }
  } mappedInput;
  GfxRenderer& renderer;
  explicit HomeActivity(GfxRenderer& target) : renderer(target) {}
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

// PHASE_TWO_BOUNDARIES
