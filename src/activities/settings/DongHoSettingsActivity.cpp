#include "DongHoSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstdio>
#include <memory>

#include "ClockOffsetActivity.h"
#include "ClockSyncActivity.h"
#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "MenuFavorites.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"

namespace fui = freeink::ui;

namespace {
enum Dong { DINH_DANG_GIO = 0, CHENH_LECH_UTC, DONG_BO_NGAY, TU_DO_MUI_GIO };

constexpr StrId TEN_DONG[DongHoSettingsActivity::SO_DONG] = {StrId::STR_CLOCK_FORMAT, StrId::STR_CLOCK_UTC_OFFSET,
                                                             StrId::STR_CLOCK_SYNC_NOW, StrId::STR_CLOCK_AUTO_TIMEZONE};

constexpr int CLOCK_FORMAT_ITEMS = 2;
constexpr StrId clockFormatNames[CLOCK_FORMAT_ITEMS] = {StrId::STR_CLOCK_FORMAT_24H, StrId::STR_CLOCK_FORMAT_12H};

std::string formatUtcOffset(uint8_t biasedQ) {
  // biasedQ tinh theo buoc 15 phut, lech 48 (48 = UTC+0).
  if (biasedQ > 104) biasedQ = 48;
  const int totalMinutes = (static_cast<int>(biasedQ) - 48) * 15;
  const bool neg = totalMinutes < 0;
  const int absMinutes = neg ? -totalMinutes : totalMinutes;
  char buf[16];
  snprintf(buf, sizeof(buf), "GMT%c%d:%02d", neg ? '-' : '+', absMinutes / 60, absMinutes % 60);
  return buf;
}
}  // namespace

DongHoSettingsActivity::DongHoSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("DongHoSettings", renderer, mappedInput) {}

void DongHoSettingsActivity::onEnter() {
  UiListActivity::onEnter();
  if (SETTINGS.clockUtcOffsetQ > 104) SETTINGS.clockUtcOffsetQ = 48;
  if (SETTINGS.clockFormat >= CLOCK_FORMAT_ITEMS) SETTINGS.clockFormat = 0;
  for (int i = 0; i < SO_DONG; i++) {
    rowItems_[i].label = I18N.get(TEN_DONG[i]);
    rowItems_[i].actionValue = static_cast<int16_t>(i);
  }
}

void DongHoSettingsActivity::activateIndex(const int index) {
  nav.selected = index;
  app.clearTapFlash();
  switch (index) {
    case DINH_DANG_GIO:
      // Hai lua chon: doi tai cho (nguong trinh chon la 4).
      SETTINGS.clockFormat = (SETTINGS.clockFormat + 1) % CLOCK_FORMAT_ITEMS;
      SETTINGS.saveToFile();
      break;
    case CHENH_LECH_UTC:
      if (SETTINGS.clockAutoTimezone) return;
      // Man chinh lech gio tu luu khi thoat.
      startActivityForResult(std::make_unique<ClockOffsetActivity>(renderer, mappedInput), nullptr);
      return;
    case DONG_BO_NGAY:
      startActivityForResult(std::make_unique<ClockSyncActivity>(renderer, mappedInput), nullptr);
      return;
    case TU_DO_MUI_GIO:
      SETTINGS.clockAutoTimezone = !SETTINGS.clockAutoTimezone;
      SETTINGS.saveToFile();
      if (SETTINGS.clockAutoTimezone) {
        startActivityForResult(std::make_unique<ClockSyncActivity>(renderer, mappedInput), nullptr);
        return;
      }
      break;
    default:
      return;
  }
  requestUpdate();
}

std::string DongHoSettingsActivity::giaTriDong(const int index) {
  switch (index) {
    case DINH_DANG_GIO:
      return I18N.get(clockFormatNames[SETTINGS.clockFormat < CLOCK_FORMAT_ITEMS ? SETTINGS.clockFormat : 0]);
    case CHENH_LECH_UTC:
      return formatUtcOffset(SETTINGS.clockUtcOffsetQ);
    case DONG_BO_NGAY:
      return SETTINGS.clockHasBeenSynced ? tr(STR_CLOCK_SYNCED) : tr(STR_NOT_SET);
    case TU_DO_MUI_GIO:
      return SETTINGS.clockAutoTimezone ? tr(STR_TIMEZONE_AUTO) : tr(STR_TIMEZONE_MANUAL);
    default:
      return "";
  }
}

void DongHoSettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  for (int i = 0; i < SO_DONG; i++) {
    rowItems_[i].enabled = i != CHENH_LECH_UTC || !SETTINGS.clockAutoTimezone;
    rowValues_[i] = giaTriDong(i);
    rowItems_[i].value = rowValues_[i].empty() ? nullptr : rowValues_[i].c_str();
  }
  fui::ListProps props;
  props.items = rowItems_;
  props.count = SO_DONG;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // nut vat ly o loop()
  props.valueInset = 8;
  props.labelText = uiMenuLabelText(screen.theme());
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}

void DongHoSettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  drawNavigationHeader(tr(STR_CLOCK));
  renderUi();
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

std::string DongHoSettingsActivity::favoriteKey(int row) const { return menufavorites::keyFor("clock", 0, row); }
