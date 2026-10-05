#pragma once
#include <I18n.h>
#include <SdCardFontRegistry.h>

#include <cstdint>
#include <array>
#include <atomic>
#include <string>
#include <vector>

#include "TextSettingsPreview.h"
#include "activities/UiTabListActivity.h"
#include "components/OptionPopup.h"
#include "shells/ugly/UglyQuestionSheet.h"
#include "components/themes/BaseTheme.h"

// Reader text settings with a shared live preview pane: tab bar
// (Font | Size | Layout | Style) is position 0 of the Up/Down nav ring, same
// idiom as SettingsActivity. Family/Size rows apply on Confirm; Layout/Style
// rows toggle or open an OptionPopup picker. (Tab::Family/Style are the enum
// names for the Font/Style tabs.)
struct SettingInfo;

class TextSettingsActivity final : public UiTabListActivity {
 public:
  static std::string layoutValueText(int row);
  static std::string styleValueText(int row);
  enum class Tab : uint8_t { Family, Size, Layout, Style, Count };

  TextSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const SdCardFontRegistry* registry,
                       Tab initialTab = Tab::Family);

  void onEnter() override;
  void onPause() override;
  void onResume() override;
  bool handleHomeGesture() override;
  void restoreNavigation(const MenuNavigationState& state) override;
  std::string navigationLabel() const override { return I18N.get(StrId::STR_TEXT_SETTINGS); }
  void render(RenderLock&&) override;

 private:
  // Row indices per tab. enum class (not plain enum) so a LayoutRow can't be
  // silently confused with a StyleRow of equal value.
  // Layout order fixed by the approved plan: the four spacing kinds first, then
  // alignment, margin and paragraph indent. WordSpacing sits between letter and
  // paragraph spacing. No CharacterSpacing row: that is upstream's #3528
  // percent/pixel-offset redesign, not taken (see RESOLUTION.md) - Tenor's own
  // LetterSpacing already covers inter-character spacing.
  enum class LayoutRow {
    LineSpacing,
    LetterSpacing,
    WordSpacing,
    ParaSpacing,
    Alignment,
    ScreenMargin,
    ParaIndent,
    Count
  };
  enum class StyleRow { FocusReading, Hyphenation, EmbeddedStyle, AntiAliasing, InkWeight, Count };

  // --- UiTabListActivity contract ---
  int listCount() const override;
  int tabCount() const override { return static_cast<int>(Tab::Count); }
  int activeTab() const override { return static_cast<int>(tab_); }
  const char* tabLabel(int index) const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onTabAction(int index) override;
  void stepTab(int direction) override { switchTab(direction); }
  // Tenor's render() (settled-list preview pane, TENOR_PRESS_PROBE timing) fully
  // replaces the base sequence, so drawChrome()/drawFooter() are not overridden
  // here (see render() below and RESOLUTION.md).
  bool handleButtons() override;
  bool handleCustomInput() override;
  void pollTilt() override;
  int favoriteSelectedRow() override;
  bool allowsTiltTabNavigation() const override { return !optionPopup_.isActive(); }
  ugly::QuestionSheet form_;
  struct FormEvent {
    enum class Type : uint8_t { Key, Tap, Hold, Pin } type = Type::Key;
    ugly::QuestionSheet::Key key = ugly::QuestionSheet::Key::Confirm;
    int16_t x = 0, y = 0;
    uint32_t surface = 0;
  };
  std::array<FormEvent, 8> formQueue_{};
  uint8_t formHead_ = 0, formCount_ = 0;
  std::atomic<bool> formPaintReady_{false}, saveFailed_{false}, formPinFailed_{false};
  uint32_t formSurface_ = 0;
  std::atomic<uint32_t> formVisibleSurface_{0};
  void queueForm(FormEvent event);
  void bindForm();  // Caller owns RenderLock.
  void focusForm(int row);  // Caller owns RenderLock.
  void applyFormIntent(const ugly::QuestionSheet::Intent& intent, bool home = false);
  void prepareFormQuip(int row, int candidate);
  bool saveSettings(bool repaint = true);
  static ugly::QuestionSheet::Row formRow(void* context, int row);
  static void formLabel(void* context, int row, int option, char* out, size_t size);

  bool supportsFavorites() const override { return true; }
  std::string favoriteKey(int row) const override;
  int focusFavorite(const std::string& key) override;

  bool applyFamily(int listIndex);
  bool applySize(int listIndex);
  // Repopulates sizes_ (and currentSizeIndex_) from the active family's
  // installed point sizes. Call after any family change.
  void rebuildSizeList();
  void confirmLayoutRow(int row);
  void confirmStyleRow(int row);
  // Applies the row at the given list index for the active tab (Confirm and tap share this).
  void activateRow(int row);
  bool applyChosenValue(Tab tab, int row, int option, bool repaint = true);
  static int formIndex(Tab tab, int row);
  static Tab formTab(int row);
  static int formLocalRow(int row);
  static const SettingInfo* formSetting(int row);

  // Button-hint label for Confirm at the current ring position.
  const char* confirmLabelText() const;
  // True when the focused list row is a setting the preview cannot reflect.
  bool focusedRowHasNoPreview() const;
  void updatePreviewGeometry();
  void switchTab(int direction = 1);

  // Row storage for the active tab: rowItems_ (label/actionValue) is
  // rebuilt only when the tab or its backing data changes (rebuildRowItems(),
  // called from onEnter()/onTabAction()/switchTab()); rowValues_ holds the
  // live per-row value text, refreshed every buildScreen() call by assigning
  // into the existing strings (no vector growth), so steady-state rendering
  // never allocates/frees row storage.
  std::vector<std::string> rowValues_;
  std::vector<freeink::ui::ListItem> rowItems_;
  void rebuildRowItems();

  struct FontEntry {
    std::string name;
    bool isBuiltin;
    uint8_t settingIndex;
  };

  struct SizeEntry {
    std::string name;  // the point size, rendered for display ("14 pt")
    uint8_t pointSize;
  };

  const SdCardFontRegistry* registry_;
  OptionPopup optionPopup_;
  OptionPopup* tiltPopup() override { return &optionPopup_; }
  std::vector<FontEntry> fonts_;
  std::vector<SizeEntry> sizes_;
  textsettings::PreviewLayout previewLayout_;  // cached preview line layout; relaid only on setting/geometry change

  Tab tab_;
  int currentFamilyIndex_ = 0;
  int currentSizeIndex_ = 0;

  ThemeMetrics metrics_ = {};
  int afterHeader = 0;
  int bottomReserved = 0;
  int usableHeight = 0;
  int previewHeight = 0;
};
