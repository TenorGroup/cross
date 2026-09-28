#include "ClockSettingsActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>
#include <memory>

#include "ClockSyncActivity.h"
#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "MenuFavorites.h"
#include "TimezonePickerActivity.h"
#include "components/UITheme.h"
#include "network/TimezoneLookup.h"
#include "util/Timezones.h"

namespace fui = freeink::ui;

namespace {
enum MenuItem {
  ITEM_TIMEZONE = 0,
  ITEM_DST,
  ITEM_FORMAT,
  ITEM_SHOW_ON_HOME,
  ITEM_SYNC,
  ITEM_AUTO_TIMEZONE,
};

const StrId menuNames[ClockSettingsActivity::ITEM_COUNT] = {
    StrId::STR_TIMEZONE,        StrId::STR_CLOCK_DST,          StrId::STR_CLOCK_FORMAT,
    StrId::STR_CLOCK_IN_HEADER, StrId::STR_CLOCK_SYNC_NOW,     StrId::STR_CLOCK_AUTO_TIMEZONE,
};

const StrId dstNames[CrossPointSettings::CLOCK_DST_MODE_COUNT] = {StrId::STR_CLOCK_DST_AUTO, StrId::STR_STATE_ON,
                                                                  StrId::STR_STATE_OFF};
}  // namespace

ClockSettingsActivity::ClockSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("ClockSettings", renderer, mappedInput) {}

void ClockSettingsActivity::onEnter() {
  UiListActivity::onEnter();
  for (int i = 0; i < ITEM_COUNT; i++) {
    rowItems_[i].label = I18N.get(menuNames[i]);
    rowItems_[i].actionValue = static_cast<int16_t>(i);
  }
}

const char* ClockSettingsActivity::headerTitle() const { return tr(STR_CLOCK); }

void ClockSettingsActivity::activateIndex(const int index) {
  nav.selected = index;
  app.clearTapFlash();
  switch (index) {
    case ITEM_TIMEZONE:
      // Auto timezone owns the zone while it is on; turn it off first (the
      // Auto Timezone row below) to pick one by hand.
      if (SETTINGS.clockAutoTimezone) return;
      if (auto activity = makeUniqueNoThrow<TimezonePickerActivity>(renderer, mappedInput)) {
        startActivityForResult(std::move(activity), nullptr);
      } else {
        LOG_ERR("CLKSET", "OOM: TimezonePickerActivity");
      }
      return;
    case ITEM_DST:
      SETTINGS.clockDst = (SETTINGS.clockDst + 1) % CrossPointSettings::CLOCK_DST_MODE_COUNT;
      timezones::applyToClock();
      break;
    case ITEM_FORMAT:
      SETTINGS.clockFormat = (SETTINGS.clockFormat + 1) % 2;
      break;
    case ITEM_SHOW_ON_HOME:
      SETTINGS.clockShowInHeader = (SETTINGS.clockShowInHeader + 1) % 2;
      break;
    case ITEM_SYNC:
      if (auto activity = makeUniqueNoThrow<ClockSyncActivity>(renderer, mappedInput)) {
        startActivityForResult(std::move(activity), nullptr);
      } else {
        LOG_ERR("CLKSET", "OOM: ClockSyncActivity");
      }
      return;
    case ITEM_AUTO_TIMEZONE:
      SETTINGS.clockAutoTimezone = !SETTINGS.clockAutoTimezone;
      SETTINGS.saveToFile();
      if (SETTINGS.clockAutoTimezone) {
        // Look the zone up immediately, over whatever radio connection is
        // already up, rather than waiting for the next WiFi connect.
        if (timezone_lookup::updateOffset()) SETTINGS.saveToFile();
        timezones::applyToClock();
      }
      requestUpdate();
      return;
    default:
      return;
  }
  SETTINGS.saveToFile();
  requestUpdate();
}

std::string ClockSettingsActivity::giaTriDong(const int index) {
  switch (index) {
    case ITEM_TIMEZONE:
      return timezones::table()[timezones::activeIndex()].name;
    case ITEM_DST: {
      const uint8_t dst = SETTINGS.clockDst < CrossPointSettings::CLOCK_DST_MODE_COUNT ? SETTINGS.clockDst : uint8_t{0};
      return I18N.get(dstNames[dst]);
    }
    case ITEM_FORMAT:
      return SETTINGS.clockFormat == 1 ? tr(STR_CLOCK_FORMAT_12H) : tr(STR_CLOCK_FORMAT_24H);
    case ITEM_SHOW_ON_HOME:
      return SETTINGS.clockShowInHeader ? tr(STR_SHOW) : tr(STR_HIDE);
    case ITEM_SYNC: {
      // The sync row's value is the current time itself: it confirms the sync,
      // previews format/zone changes, and reads "Not Set" until the first sync.
      char buf[9];  // "HH:MM PM" + NUL
      if (SETTINGS.clockHasBeenSynced && halClock.formatTime(buf, sizeof(buf), SETTINGS.clockFormat == 1)) return buf;
      return tr(STR_NOT_SET);
    }
    case ITEM_AUTO_TIMEZONE:
      return SETTINGS.clockAutoTimezone ? tr(STR_TIMEZONE_AUTO) : tr(STR_TIMEZONE_MANUAL);
    default:
      return "";
  }
}

std::string ClockSettingsActivity::favoriteKey(const int row) const { return menufavorites::keyFor("clock", 0, row); }

void ClockSettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  static std::string values[ITEM_COUNT];
  for (int i = 0; i < ITEM_COUNT; i++) {
    values[i] = giaTriDong(i);
    rowItems_[i].value = values[i].empty() ? nullptr : values[i].c_str();
  }
  // Manual timezone picking is unreachable while auto timezone drives the zone.
  rowItems_[ITEM_TIMEZONE].enabled = !SETTINGS.clockAutoTimezone;

  fui::ListProps props;
  props.items = rowItems_;
  props.count = ITEM_COUNT;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}
