#include "AboutActivity.h"

#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalFrontlight.h>
#include <HalPowerManager.h>
#include <HalTiltSensor.h>
#include <I18n.h>
#include <esp_mac.h>

#include <cstdio>

#include "MappedInputManager.h"
#include "components/TenorMenuChrome.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {
enum MenuItem {
  ITEM_DEVICE = 0,
  ITEM_FIRMWARE,
  ITEM_CHIP,
  ITEM_FLASH,
  ITEM_DISPLAY,
  ITEM_RESOLUTION,
  ITEM_TOUCH,
  ITEM_FRONTLIGHT,
  ITEM_RTC,
  ITEM_IMU,
  ITEM_MAC,
  // Raw BQ27220 registers (X3 only, appended past ITEM_COUNT); see AboutActivity.h.
  ITEM_GAUGE_VOLTAGE = AboutActivity::ITEM_COUNT,
  ITEM_GAUGE_CURRENT,
  ITEM_GAUGE_REMAINING,
  ITEM_GAUGE_FULL_CHARGE,
  ITEM_GAUGE_DESIGN_CAPACITY,
  ITEM_GAUGE_SOC,
  ITEM_GAUGE_SOH,
  ITEM_GAUGE_CYCLE_COUNT,
  ITEM_GAUGE_FLAGS,
};

// Deliberately hardcoded English, exempt from the tr() rule: support reads
// these screenshots across every device language, so the labels must be
// identical on every unit.
const char* const menuNames[AboutActivity::ITEM_COUNT] = {
    "Device", "Firmware",          "Chip",        "Flash", "Display Controller", "Resolution", "Touch", "Frontlight",
    "RTC",    "Tilt Sensor (IMU)", "MAC Address",
};

// Translated via I18n: unlike menuNames above, these are diagnostic labels a user
// reads and photographs on their own device, not a support screenshot reference.
const StrId gaugeMenuNames[AboutActivity::GAUGE_ITEM_COUNT] = {
    StrId::STR_ABOUT_GAUGE_VOLTAGE,          StrId::STR_ABOUT_GAUGE_CURRENT,
    StrId::STR_ABOUT_GAUGE_REMAINING,        StrId::STR_ABOUT_GAUGE_FULL_CHARGE,
    StrId::STR_ABOUT_GAUGE_DESIGN_CAPACITY,  StrId::STR_ABOUT_GAUGE_SOC,
    StrId::STR_ABOUT_GAUGE_SOH,              StrId::STR_ABOUT_GAUGE_CYCLE_COUNT,
    StrId::STR_ABOUT_GAUGE_FLAGS,
};

// "-" when the gauge cache has no reading yet (About opened before the loop task's first
// 30s-throttled poll landed); never 0, which would look like real telemetry.
constexpr char NO_GAUGE_READING[] = "-";

// Chip part numbers, not user prose — deliberately untranslated.
const char* displayControllerName(const BoardConfig::DisplayController c) {
  switch (c) {
    case BoardConfig::DisplayController::SSD1677:
      return "SSD1677";
    case BoardConfig::DisplayController::UC8253:
      return "UC8253";
    case BoardConfig::DisplayController::ED2208:
      return "ED2208";
    case BoardConfig::DisplayController::LgfxEpd:
      return "LovyanGFX EPD";
    case BoardConfig::DisplayController::IT8951:
      return "IT8951";
    case BoardConfig::DisplayController::UC8279:
      return "UC8279";
    case BoardConfig::DisplayController::UC8179:
      return "UC8179";
    case BoardConfig::DisplayController::UC8279C:
      return "UC8279C";
  }
  return "?";
}

const char* touchControllerName(const BoardConfig::TouchController c) {
  switch (c) {
    case BoardConfig::TouchController::None:
      return nullptr;
    case BoardConfig::TouchController::Chsc6x:
      return "CHSC6X";
    case BoardConfig::TouchController::Gt911:
      return "GT911";
    case BoardConfig::TouchController::Ft5x06:
      return "FT5x06";
    case BoardConfig::TouchController::Ft6336u:
      return "FT6336U";
    case BoardConfig::TouchController::Gslx680:
      return "GSLX680";
  }
  return nullptr;
}
}  // namespace

AboutActivity::AboutActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("About", renderer, mappedInput) {}

// Touch: the screen's name and its first two rows in the device language (founder audit 06/10); the
// hardware rows keep the support names above.
const char* AboutActivity::headerTitle() const { return tenorchrome::kTouchShell ? tr(STR_ABOUT) : "About"; }

void AboutActivity::onEnter() {
  UiListActivity::onEnter();
  for (int i = 0; i < ITEM_COUNT; i++) {
    rowItems_[i].label = menuNames[i];
    rowItems_[i].actionValue = static_cast<int16_t>(i);
  }
  if (tenorchrome::kTouchShell) {
    rowItems_[ITEM_DEVICE].label = tr(STR_CAT_DEVICE);
    rowItems_[ITEM_FIRMWARE].label = tr(STR_ABOUT_FIRMWARE);
  }

  hasGauge_ = HalPowerManager::hasBq27220Gauge();
  if (hasGauge_) {
    for (int i = 0; i < GAUGE_ITEM_COUNT; i++) {
      const int row = ITEM_COUNT + i;
      // gaugeMenuNames holds StrId values, not identifiers, so this calls I18n directly
      // instead of the tr(id) macro (which stringifies its argument as `StrId::id`).
      rowItems_[row].label = I18n::getInstance().get(gaugeMenuNames[i]);
      rowItems_[row].actionValue = static_cast<int16_t>(row);
    }
  }

  char buf[32];
  // Hardware and firmware information is fixed after boot; fill it once here.
  // BoardConfig::ACTIVE reflects RUNTIME detection: selectDevice() picked the
  // profile and applyXteinkDisplayController() may have promoted the display
  // controller to the panel actually found on the bus.
  rowValues_[ITEM_DEVICE] = BoardConfig::ACTIVE.name;
  rowValues_[ITEM_FIRMWARE] = CROSSPOINT_VERSION;
  snprintf(buf, sizeof(buf), "%s rev %u", ESP.getChipModel(), static_cast<unsigned>(ESP.getChipRevision()));
  rowValues_[ITEM_CHIP] = buf;
  snprintf(buf, sizeof(buf), "%u MB", static_cast<unsigned>(ESP.getFlashChipSize() / (1024u * 1024u)));
  rowValues_[ITEM_FLASH] = buf;
  rowValues_[ITEM_DISPLAY] = displayControllerName(BoardConfig::ACTIVE.displayController);
  snprintf(buf, sizeof(buf), "%ux%u", static_cast<unsigned>(BoardConfig::ACTIVE.displayWidth),
           static_cast<unsigned>(BoardConfig::ACTIVE.displayHeight));
  rowValues_[ITEM_RESOLUTION] = buf;
  const char* touch = touchControllerName(BoardConfig::ACTIVE.touch.controller);
  rowValues_[ITEM_TOUCH] = touch ? touch : "No";
  rowValues_[ITEM_FRONTLIGHT] = Frontlight.present() ? "Yes" : "No";
  rowValues_[ITEM_RTC] = halClock.isAvailable() ? "Yes" : "No";
  rowValues_[ITEM_IMU] = halTiltSensor.isAvailable() ? "Yes" : "No";
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  rowValues_[ITEM_MAC] = buf;
  // Gauge rows (if any) are filled by buildScreen()'s refreshGaugeRows() call, which runs
  // right before every render, this first one included.
}

// Raw numbers, deliberately not run through getDisplayedBatteryPercentage()'s smoothing: this
// screen exists so a user photo can be compared against the BQ27220 TRM directly. Reads only
// HalPowerManager's cache
// (populated by the loop task in pollGauge(), at most once every 30s); no I2C from here.
void AboutActivity::refreshGaugeRows() {
  const auto& g = powerManager.gaugeDiagnostics();
  if (!g.valid) {
    for (int i = 0; i < GAUGE_ITEM_COUNT; i++) rowValues_[ITEM_COUNT + i] = NO_GAUGE_READING;
    return;
  }
  char buf[24];
  snprintf(buf, sizeof(buf), "%u mV", static_cast<unsigned>(g.millivolts));
  rowValues_[ITEM_GAUGE_VOLTAGE] = buf;
  snprintf(buf, sizeof(buf), "%d mA", static_cast<int>(g.averageCurrentMa));
  rowValues_[ITEM_GAUGE_CURRENT] = buf;
  snprintf(buf, sizeof(buf), "%u mAh", static_cast<unsigned>(g.remainingCapacityMah));
  rowValues_[ITEM_GAUGE_REMAINING] = buf;
  snprintf(buf, sizeof(buf), "%u mAh", static_cast<unsigned>(g.fullChargeCapacityMah));
  rowValues_[ITEM_GAUGE_FULL_CHARGE] = buf;
  snprintf(buf, sizeof(buf), "%u mAh", static_cast<unsigned>(g.designCapacityMah));
  rowValues_[ITEM_GAUGE_DESIGN_CAPACITY] = buf;
  snprintf(buf, sizeof(buf), "%u%%", static_cast<unsigned>(g.stateOfChargePercent));
  rowValues_[ITEM_GAUGE_SOC] = buf;
  snprintf(buf, sizeof(buf), "%u%%", static_cast<unsigned>(g.stateOfHealthPercent));
  rowValues_[ITEM_GAUGE_SOH] = buf;
  snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(g.cycleCount));
  rowValues_[ITEM_GAUGE_CYCLE_COUNT] = buf;
  snprintf(buf, sizeof(buf), "0x%04X", static_cast<unsigned>(g.statusFlags));
  rowValues_[ITEM_GAUGE_FLAGS] = buf;
}

void AboutActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  const int count = listCount();
  if (hasGauge_) refreshGaugeRows();  // cheap: formats the loop task's cache, no I2C here
  for (int i = 0; i < count; i++) {
    rowItems_[i].value = rowValues_[i].c_str();
  }

  fui::ListProps props;
  props.items = rowItems_;
  props.count = count;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 1;
  syncListViewport(screen, props);
  screen.list(props);
}
