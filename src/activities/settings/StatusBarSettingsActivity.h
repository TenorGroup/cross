#pragma once
#include <I18n.h>

#include <array>
#include <atomic>
#include <string>

#include "activities/UiListActivity.h"
#include "shells/ugly/UglyQuestionSheet.h"

// Reader status bar configuration activity
class StatusBarSettingsActivity final : public UiListActivity {
 public:
  explicit StatusBarSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  // Must equal the number of named rows in the .cpp (static_assert'd there).
  static constexpr int MAX_STATUS_BAR_ITEMS = 4;

  void onEnter() override;
  void onPause() override;
  void onResume() override;
  bool handleHomeGesture() override;
  void restoreNavigation(const MenuNavigationState& state) override;
  std::string navigationLabel() const override;
  void render(RenderLock&&) override;

 private:
  ugly::QuestionSheet form_;
  struct FormEvent {
    enum class Type : uint8_t { Key, Tap, Hold, Pin, Strike } type = Type::Key;
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
  // The option a fresh device holds for question `row`, -1 when the question has no default to go back to.
  int defaultOption(int row) const;
  void bindForm();  // Caller owns RenderLock.
  void focusForm(int row);  // Caller owns RenderLock.
  void applyFormIntent(const ugly::QuestionSheet::Intent& intent, bool home = false);
  void prepareFormQuip(int row, int candidate);
  bool saveSettings(bool repaint = true);
  static ugly::QuestionSheet::Row formRow(void* context, int row);
  static void formLabel(void* context, int row, int option, char* out, size_t size);

  bool supportsFavorites() const override { return choiceRow_ < 0; }
  std::string favoriteKey(int row) const override;
  int focusFavorite(const std::string& key) override;
  OptionPopup optionPopup_;
  int settingsChoiceCount(int row) const override;
  bool handleButtons() override;
  bool rowOpens(int) const override { return choiceRow_ < 0; }
  bool listFramed() const override { return choiceRow_ < 0; }
  freeink::ui::ListNav& activeNav() override { return choiceRow_ < 0 ? nav : choiceNav_; }
  void onBackButton() override;
  void closeChoices();
  void drawChoiceFrame();
  int choiceRow_ = -1;
  int choiceTop_ = 0, choiceBottom_ = 0, choiceRowHeight_ = 0;
  freeink::ui::ListNav choiceNav_;
  freeink::ui::ListItem choiceItems_[7]{};

  int visibleItemCount = 0;

  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleCustomInput() override;
  void pollTilt() override;
  int favoriteSelectedRow() override;

  std::string rowValueText(int index);

  void handleSelection();
  bool applyChosenValue(int row, int option, bool repaint = true);

  // Row storage: MAX_STATUS_BAR_ITEMS is a compile-time constant, so
  // fixed-capacity storage avoids any heap allocation for the row list.
  // Labels are set once in onEnter() (visibleItemCount is decided there);
  // buildScreen() only refreshes the live value text (rowValues_) by
  // assigning into the existing strings (no array growth).
  std::string rowValues_[MAX_STATUS_BAR_ITEMS];
  freeink::ui::ListItem rowItems_[MAX_STATUS_BAR_ITEMS]{};
};
