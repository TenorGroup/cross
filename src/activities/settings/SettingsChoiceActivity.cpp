#include "SettingsChoiceActivity.h"

#include "components/UITheme.h"
#include "components/UIThemeTokens.h"

SettingsChoiceActivity::SettingsChoiceActivity(GfxRenderer& renderer, MappedInputManager& input, std::string title,
                                               std::vector<std::string> labels, const int selected,
                                               std::function<void(int)> onSelect)
    : UiListActivity("SettingsChoices", renderer, input), title_(std::move(title)), labels_(std::move(labels)),
      rows_(labels_.size()), selected_(kepConTro(selected, listCount())), onSelect_(std::move(onSelect)) {}

void SettingsChoiceActivity::onEnter() {
  UiListActivity::onEnter();
  nav.selected = selected_;
  nav.followOnBuild = true;
}

void SettingsChoiceActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(freeink::ui::Insets{
      static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
      static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  for (int index = 0; index < listCount(); ++index) {
    rows_[index].label = labels_[index].c_str();
    rows_[index].actionValue = static_cast<int16_t>(index);
    rows_[index].chosen = index == selected_;
  }
  freeink::ui::ListProps props;
  props.items = rows_.data();
  props.count = static_cast<uint16_t>(rows_.size());
  props.action = ACTION_ROW;
  props.inputMask = freeink::ui::InputTouch;
  props.labelText = uiMenuLabelText(screen.theme());
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}

void SettingsChoiceActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  onSelect_(index);
  selected_ = index;
  nav.selected = index;
  requestUpdate();
}
