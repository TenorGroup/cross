#pragma once
#include <I18n.h>

#include <atomic>
#include <functional>
#include <span>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "activities/UiTabListActivity.h"
#include "activities/settings/SettingsTabs.h"
#include "components/OptionPopup.h"

enum class SettingType { TOGGLE, ENUM, ACTION, VALUE, STRING };

// Danh sach dong hanh dong, va NHA cua tung dong, nam o SettingsTabs.h: do la du
// lieu thuan nen bai kiem chay duoc tren may de ban (test/settings_tabs). Includes
// upstream's ClockSettings (#3562), HomeButton (#3516) and About (#3563) alongside
// Tenor's own DeviceName/FileTransfer/BrowseOPDS/BlePageTurner actions.
using SettingAction = settingstabs::Action;

struct SettingInfo {
  StrId nameId;
  SettingType type;
  uint8_t CrossPointSettings::* valuePtr = nullptr;
  std::vector<StrId> enumValues;
  std::span<const StrId> staticEnumValues;
  std::vector<std::string> enumStringValues;  // runtime alternative to StrId enumValues (for SD card fonts etc.)
  SettingAction action = SettingAction::None;

  struct ValueRange {
    uint8_t min;
    uint8_t max;
    uint8_t step;
  };
  ValueRange valueRange = {};

  const char* key = nullptr;             // JSON API key (nullptr for ACTION types)
  StrId category = StrId::STR_NONE_OPT;  // Category for web UI grouping
  bool obfuscated = false;               // Save/load via base64 obfuscation (passwords)
  bool inTextSettings = false;           // Surfaced in the Text Settings screen; hidden from the flat Reader list

  // Direct char[] string fields (for settings stored in CrossPointSettings)
  size_t stringOffset = 0;
  size_t stringMaxLen = 0;

  // Dynamic accessors (for settings stored outside CrossPointSettings, e.g. KOReaderCredentialStore)
  std::function<uint8_t()> valueGetter;
  std::function<void(uint8_t)> valueSetter;
  std::function<std::string()> stringGetter;
  std::function<void(const std::string&)> stringSetter;

  SettingInfo& withObfuscated() {
    obfuscated = true;
    return *this;
  }

  SettingInfo& withTextSettings() {
    inTextSettings = true;
    return *this;
  }

  std::span<const StrId> enumLabels() const {
    return staticEnumValues.empty() ? std::span<const StrId>(enumValues) : staticEnumValues;
  }

  static SettingInfo Toggle(StrId nameId, uint8_t CrossPointSettings::* ptr, const char* key = nullptr,
                            StrId category = StrId::STR_NONE_OPT) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::TOGGLE;
    s.valuePtr = ptr;
    s.key = key;
    s.category = category;
    return s;
  }

  static SettingInfo Enum(StrId nameId, uint8_t CrossPointSettings::* ptr, std::vector<StrId> values,
                          const char* key = nullptr, StrId category = StrId::STR_NONE_OPT) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::ENUM;
    s.valuePtr = ptr;
    s.enumValues = std::move(values);
    s.key = key;
    s.category = category;
    return s;
  }

  static SettingInfo StaticEnum(StrId nameId, uint8_t CrossPointSettings::* ptr, std::span<const StrId> values,
                                const char* key = nullptr, StrId category = StrId::STR_NONE_OPT) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::ENUM;
    s.valuePtr = ptr;
    s.staticEnumValues = values;
    s.key = key;
    s.category = category;
    return s;
  }

  static SettingInfo Action(StrId nameId, SettingAction action) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::ACTION;
    s.action = action;
    return s;
  }

  static SettingInfo Value(StrId nameId, uint8_t CrossPointSettings::* ptr, const ValueRange valueRange,
                           const char* key = nullptr, StrId category = StrId::STR_NONE_OPT) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::VALUE;
    s.valuePtr = ptr;
    s.valueRange = valueRange;
    s.key = key;
    s.category = category;
    return s;
  }

  static SettingInfo String(StrId nameId, const char* ptr, size_t maxLen, const char* key = nullptr,
                            StrId category = StrId::STR_NONE_OPT) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::STRING;
    s.stringOffset = (size_t)ptr - (size_t)&SETTINGS;
    s.stringMaxLen = maxLen;
    s.key = key;
    s.category = category;
    return s;
  }

  static SettingInfo DynamicEnum(StrId nameId, std::vector<StrId> values, std::function<uint8_t()> getter,
                                 std::function<void(uint8_t)> setter, const char* key = nullptr,
                                 StrId category = StrId::STR_NONE_OPT) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::ENUM;
    s.enumValues = std::move(values);
    s.valueGetter = std::move(getter);
    s.valueSetter = std::move(setter);
    s.key = key;
    s.category = category;
    return s;
  }

  static SettingInfo DynamicString(StrId nameId, std::function<std::string()> getter,
                                   std::function<void(const std::string&)> setter, const char* key = nullptr,
                                   StrId category = StrId::STR_NONE_OPT) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::STRING;
    s.stringGetter = std::move(getter);
    s.stringSetter = std::move(setter);
    s.key = key;
    s.category = category;
    return s;
  }
};

inline bool settingOpensNext(const SettingInfo& setting) {
  if (setting.nameId == StrId::STR_TIME_TO_SLEEP) return true;
  if (setting.type == SettingType::ACTION) return setting.action != SettingAction::None;
  if (setting.type != SettingType::ENUM) return false;
  const size_t count = setting.valuePtr == nullptr && !setting.enumStringValues.empty()
                           ? setting.enumStringValues.size()
                           : setting.enumLabels().size();
  return settingstabs::moTrinhChon(static_cast<int>(count));
}

class SettingsActivity final : public UiTabListActivity {
  int selectedCategoryIndex = 0;  // Currently selected category
  const bool fromHomeGroup;
  const int theBanDau;  // the mo san, do nguoi goi dat
  int settingsCount = 0;
  int pendingSiblingIndex = -1;
  bool releaseListsForFontDownload_ = false;

  // Per-category settings derived from shared list + device-only actions
  std::vector<SettingInfo> displaySettings;
  std::vector<SettingInfo> readerSettings;
  std::vector<SettingInfo> controlsSettings;
  std::vector<SettingInfo> systemSettings;
  std::vector<SettingInfo> deviceSettings;
  std::vector<SettingInfo> otherSettings;
  std::vector<SettingInfo> keyboardSettings;
  std::vector<SettingInfo> sleepSettings;
  std::vector<SettingInfo> motionSettings;
  const std::vector<SettingInfo>* currentSettings = nullptr;

  // Mot cho duy nhat noi the nao giu danh sach nao. Truoc 14/09/2026 phep nay chep
  // ra hai cho (rebuildSettingsLists va selectCategory), va hai ban sao do la kieu
  // vo am tham khi them mot the moi.
  std::vector<SettingInfo>& danhSachCuaThe(settingstabs::Tab tab);

  std::atomic<bool> saveFailed{false};
  bool saveSettings();

  bool preserveQuickResumeTimeoutOn = false;
  bool quickResumeTimeoutAutoEnabled = false;

  OptionPopup optionPopup;
  OptionPopup* tiltPopup() override { return &optionPopup; }

  // Row structure (label/actionValue) for *currentSettings, rebuilt only when
  // the active category or a category's setting list changes
  // (rebuildRowItems(), called from selectCategory()/rebuildSettingsLists())
  // - not on every repaint. rowValues_ holds the live per-row value text,
  // refreshed every buildScreen() call by assigning into the existing
  // strings (no vector growth).
  std::vector<std::string> rowValues_;
  std::vector<freeink::ui::ListItem> rowItems_;
  void rebuildRowItems();

  static constexpr int categoryCount = settingstabs::TAB_COUNT;

  // --- UiTabListActivity contract ---
  int listCount() const override { return settingsCount; }
  // Gestures only on a board with one (deviceSettingsTabCount).
  int tabCount() const override;
  int activeTab() const override { return selectedCategoryIndex; }
  const char* tabLabel(int index) const override {
    return I18N.get(settingstabs::tenThe(static_cast<settingstabs::Tab>(index)));
  }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onTabAction(int index) override;
  void stepTab(int direction) override;
  bool handleButtons() override;
  bool handleCustomInput() override;
  bool allowsTiltTabNavigation() const override { return !optionPopup.isActive(); }
  bool supportsFavorites() const override { return true; }
  std::string favoriteKey(int row) const override;
  int focusFavorite(const std::string& key) override;
  // Vong con tro CHI GOM DONG (S1, chot 14/09/2026 dem): khong con vi tri 0 cua thanh the.
  void navigateButtons() override;
  void nhanNhom();
  // Dap xuong nhom moi: dong da nho, chua nho thi dong 1. KHONG xoa cho nho cua nhom khac.
  void dapXuongNhom();

  void selectCategory(int categoryIndex);
  bool applyUiSettingChange(uint8_t CrossPointSettings::* valuePtr, uint8_t newValue);

  void enterCategory(int categoryIndex);
  static void veTenNhomCoMuiTen(const GfxRenderer& r, int x0, int yGiua, const char* ten);
  void toggleCurrentSetting();
  void noteValue(StrId name);  // tells the shell which row changed (shell::valueChanged)
  void openSleepTimeoutPicker();
  void rebuildSettingsLists();
  void syncQuickResumeTimeoutForSleepScreen(bool sleepScreenChanged, bool quickResumeTimeoutChanged);

  // Tenor's render() (Tenor tab chrome, sibling-tab arrows, save-failed popup)
  // fully replaces the base sequence, so drawChrome()/drawFooter() are not
  // overridden here (see render() below and RESOLUTION.md).

 public:
  static std::string settingValueText(const SettingInfo& setting);
  // Whether a row of the catalog is listed on its tab (the Home key's own rows and, with a Home key, the
  // long-press menu row are not). The one rule for this screen and the touch shell's settings page.
  static bool listedAsRow(const SettingInfo& setting);
  // The UI text size: the fonts, the setting and the theme together. False, with nothing changed, when the
  // fonts cannot be had.
  static bool applyUiTextSize(GfxRenderer& renderer, uint8_t size);
  // A row another screen may change where it stands, a toggle or a list of values written straight into
  // the setting: toggleCurrentSetting does nothing more for it than write, save and rebuild (uiTextSize
  // through applyUiTextSize, uiShell through shell::changed). Rows it treats apart (the sleep screen pair
  // keeps quick resume in step, the sleep timer has its own picker, the clock row is rebuilt) are not.
  static bool changesInPlace(const SettingInfo& s) {
    if (s.type != SettingType::TOGGLE && s.type != SettingType::ENUM) return false;
    if (!s.valuePtr && !(s.valueGetter && s.valueSetter)) return false;
    if (s.inTextSettings || s.nameId == StrId::STR_TIME_TO_SLEEP) return false;
    return s.valuePtr != &CrossPointSettings::sleepScreen && s.valuePtr != &CrossPointSettings::quickResumeSleepScreen &&
           s.valuePtr != &CrossPointSettings::statusBarClock;
  }
  // theBanDau: the mo san khi vao man. Man chinh bay cac nhom cai dat thanh dong, bam
  // mot dong la vao thang the do, khoi phai nhay the lai tu dau.
  std::string navigationMemoryKey() const override { return name + ":" + std::to_string(theBanDau); }

  explicit SettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, int theBanDau = 0,
                            bool fromHomeGroup = false);
  void onEnter() override;
  void onPause() override;
  void onResume() override;
  std::string navigationLabel() const override { return tabLabel(activeTab()); }
  bool selectSettingsSibling(int direction) override;
  bool openPendingSettingsSibling() override;
  void restoreNavigation(const MenuNavigationState& state) override;
  void onExit() override;
  void render(RenderLock&& lock) override;
};
