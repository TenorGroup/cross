#include "InfoUpdateActivity.h"

#include <Memory.h>

#include "AboutActivity.h"
#include "OtaUpdateActivity.h"
#include "PanelChip.h"
#include "SdFirmwareUpdateActivity.h"
#include "components/TenorMenuChrome.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "shells/Shell.h"

namespace fui = freeink::ui;
using Action = settingstabs::Action;

namespace {
// Row order on the screen; the chip row has no action.
constexpr Action ROW_ACTIONS[] = {Action::About, Action::None, Action::CheckForUpdates, Action::SdFirmwareUpdate};
constexpr StrId ROW_LABELS[] = {StrId::STR_ABOUT, StrId::STR_DISPLAY_CHIP, StrId::STR_CHECK_UPDATES,
                                StrId::STR_SD_FIRMWARE_UPDATE};
}  // namespace

bool infoupdate::shown() { return tenorchrome::kTouchShell && !shell::isUgly(); }

bool infoupdate::holds(const Action action, const bool chip) {
  if (!shown()) return false;
  return chip || action == Action::About || action == Action::CheckForUpdates || action == Action::SdFirmwareUpdate;
}

std::string InfoUpdateActivity::favoriteKey(const int row) const {
  if (row < 0 || row >= ROWS || ROW_ACTIONS[row] == Action::None) return {};
  return "action/" + std::to_string(static_cast<int>(ROW_ACTIONS[row]));
}

void InfoUpdateActivity::activateIndex(const int index) {
  if (index < 0 || index >= ROWS) return;
  nav.selected = index;
  std::unique_ptr<Activity> next;
  switch (ROW_ACTIONS[index]) {
    case Action::About: next = makeUniqueNoThrow<AboutActivity>(renderer, mappedInput); break;
    case Action::CheckForUpdates: next = makeUniqueNoThrow<OtaUpdateActivity>(renderer, mappedInput); break;
    case Action::SdFirmwareUpdate: next = makeUniqueNoThrow<SdFirmwareUpdateActivity>(renderer, mappedInput); break;
    default: return;
  }
  if (!next) return;
  app.clearTapFlash();
  startActivityForResult(std::move(next), nullptr);
}

void InfoUpdateActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  for (int i = 0; i < ROWS; ++i) {
    rows[i].label = I18N.get(ROW_LABELS[i]);
    rows[i].actionValue = static_cast<int16_t>(i);
    rows[i].value = i == 1 ? panelchip::current().c_str() : nullptr;
    rows[i].opensNext = i != 1;
  }
  fui::ListProps props;
  props.items = rows;
  props.count = ROWS;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch | fui::InputLongPress;
  props.labelText = uiMenuLabelText(screen.theme());
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}
