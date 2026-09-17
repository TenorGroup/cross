#include "MenuFavorites.h"

#include <BoardConfig.h>
#include <HalClock.h>
#include <HalGPIO.h>
#include <Memory.h>

#include "MenuCustomization.h"
#include "SdCardFontSystem.h"
#include "SettingsList.h"
#include "activities/settings/DongHoSettingsActivity.h"
#include "activities/settings/KOReaderSettingsActivity.h"
#include "activities/settings/OpdsServerListActivity.h"
#include "activities/settings/StatusBarSettingsActivity.h"
#include "activities/settings/TextSettingsActivity.h"
extern HalGPIO gpio;
namespace menufavorites {
namespace {
bool unavailableClock(const std::string& key) {
  const bool needsRtc = key.rfind("clock/", 0) == 0 || key == "action/14" || key == "status/statusBarClock" ||
                        key == "settings/statusBarClock" ||
                        (SETTINGS.uiTheme == CrossPointSettings::TENOR_UI && key == "action/2");
  return needsRtc && !halClock.isAvailable();
}
constexpr Descriptor ITEMS[] = {
    {"clock/clockFormat", StrId::STR_CLOCK_FORMAT, "clock", 0, 0},
    {"clock/clockUtcOffsetQ", StrId::STR_CLOCK_UTC_OFFSET, "clock", 0, 1},
    {"clock/sync", StrId::STR_CLOCK_SYNC_NOW, "clock", 0, 2},
    {"clock/clockAutoTimezone", StrId::STR_CLOCK_AUTO_TIMEZONE, "clock", 0, 3},
    {"status/statusBarChapterPageCount", StrId::STR_CHAPTER_PAGE_COUNT, "status", 0, 0},
    {"status/statusBarBookProgressPercentage", StrId::STR_BOOK_PROGRESS_PERCENTAGE, "status", 0, 1},
    {"status/statusBarProgressBar", StrId::STR_PROGRESS_BAR, "status", 0, 2},
    {"status/statusBarProgressBarThickness", StrId::STR_PROGRESS_BAR_THICKNESS, "status", 0, 3},
    {"status/statusBarTitle", StrId::STR_TITLE, "status", 0, 4},
    {"status/statusBarBattery", StrId::STR_BATTERY, "status", 0, 5},
    {"status/xtcStatusBarMode", StrId::STR_XTC_STATUS_BAR, "status", 0, 6},
    {"status/statusBarClock", StrId::STR_CLOCK, "status", 0, 7},
    {"kosync/koUsername", StrId::STR_USERNAME, "kosync", 0, 0},
    {"kosync/koPassword", StrId::STR_PASSWORD, "kosync", 0, 1},
    {"kosync/koServerUrl", StrId::STR_SYNC_SERVER_URL, "kosync", 0, 2},
    {"kosync/koMatchMethod", StrId::STR_DOCUMENT_MATCHING, "kosync", 0, 3},
    {"kosync/koSendMetadata", StrId::STR_SEND_METADATA, "kosync", 0, 4},
    {"kosync/koSyncBehavior", StrId::STR_SYNC_BEHAVIOR, "kosync", 0, 5},
    {"kosync/login", StrId::STR_AUTHENTICATE, "kosync", 0, 6},
    {"kosync/signup", StrId::STR_SIGN_UP, "kosync", 0, 7},
    {"text/fontFamily", StrId::STR_FONT_FAMILY, "text", 0, 0},
    {"text/fontSize", StrId::STR_FONT_SIZE, "text", 1, 0},
    {"text/lineSpacing", StrId::STR_LINE_SPACING, "text", 2, 0},
    {"text/letterSpacing", StrId::STR_LETTER_SPACING, "text", 2, 1},
    {"text/wordSpacing", StrId::STR_WORD_SPACING, "text", 2, 2},
    {"text/extraParagraphSpacing", StrId::STR_EXTRA_SPACING, "text", 2, 3},
    {"text/paragraphAlignment", StrId::STR_PARA_ALIGNMENT, "text", 2, 4},
    {"text/screenMargin", StrId::STR_SCREEN_MARGIN, "text", 2, 5},
    {"text/paragraphIndent", StrId::STR_PARAGRAPH_INDENT, "text", 2, 6},
    {"text/dropCapMode", StrId::STR_FOCUS_READING, "text", 3, 0},
    {"text/hyphenationEnabled", StrId::STR_HYPHENATION, "text", 3, 1},
    {"text/embeddedStyle", StrId::STR_EMBEDDED_STYLE, "text", 3, 2},
    {"text/textAntiAliasing", StrId::STR_TEXT_AA, "text", 3, 3},
    {"text/readerInkWeight", StrId::STR_READER_INK_WEIGHT, "text", 3, 4},
    {"opds/add", StrId::STR_ADD_SERVER, "opds", 0, 0},
    {"opds/opdsDownloadFolder", StrId::STR_OPDS_DOWNLOAD_FOLDER, "opds", 0, 1},
    {"opds/opdsFilenameFormat", StrId::STR_OPDS_FILENAME_FORMAT, "opds", 0, 2},
    {"action/1", StrId::STR_REMAP_FRONT_BUTTONS, "settings", 0, -1},
    {"action/2", StrId::STR_CUSTOMISE_STATUS_BAR, "settings", 0, -1},
    {"action/3", StrId::STR_KOREADER_SYNC, "settings", 0, -1},
    {"action/4", StrId::STR_OPDS_SERVERS, "settings", 0, -1},
    {"action/5", StrId::STR_WIFI_NETWORKS, "settings", 0, -1},
    {"action/6", StrId::STR_CLEAR_READING_CACHE, "settings", 0, -1},
    {"action/7", StrId::STR_CHECK_UPDATES, "settings", 0, -1},
    {"action/8", StrId::STR_SD_FIRMWARE_UPDATE, "settings", 0, -1},
    {"action/9", StrId::STR_LANGUAGE, "settings", 0, -1},
    {"action/10", StrId::STR_MANAGE_FONTS, "settings", 0, -1},
    {"action/11", StrId::STR_TEXT_SETTINGS, "settings", 0, -1},
    {"action/12", StrId::STR_KEYBOARD_LAYOUTS, "settings", 0, -1},
    {"action/13", StrId::STR_DEVICE_NAME, "settings", 0, -1},
    {"action/14", StrId::STR_CLOCK, "settings", 0, -1},
    {"action/15", StrId::STR_FILE_TRANSFER, "settings", 0, -1},
    {"action/16", StrId::STR_OPDS_BROWSER, "settings", 0, -1},
    {"action/17", StrId::STR_BLE_PAGE_TURNER, "settings", 0, -1},
};
}
const Descriptor* find(const std::string& key) {
  const char* canonical = menucustom::canonicalPinKey(key.c_str());
  for (const auto& item : ITEMS)
    if (strcmp(canonical, item.key) == 0) return &item;
  return nullptr;
}
const char* keyFor(const char* screen, int tab, int row) {
  for (const auto& item : ITEMS)
    if (strcmp(screen, item.screen) == 0 && tab == item.tab && row == item.row) return item.key;
  return "";
}
StrId label(const std::string& key, const std::vector<SettingInfo>& settings) {
  const char* canonical = menucustom::canonicalPinKey(key.c_str());
  if (canonical != key.c_str()) return label(canonical, settings);
  if (unavailableClock(key)) return StrId::STR_NONE_OPT;
  if (key.rfind("settings/", 0) == 0) {
    for (const auto& info : settings) {
      if (!info.key || key.compare(9, std::string::npos, info.key) != 0) continue;
      if (SETTINGS.uiTheme == CrossPointSettings::TENOR_UI &&
          info.valuePtr == &CrossPointSettings::hideBatteryPercentage)
        return StrId::STR_NONE_OPT;
      if (info.valuePtr == &CrossPointSettings::wakeButtons && !gpio.deviceIsX3()) return StrId::STR_NONE_OPT;
      if (info.valuePtr == &CrossPointSettings::fadingFix && (BoardConfig::isX4Pro() || BoardConfig::isX4Classic()))
        return StrId::STR_NONE_OPT;
      if (info.valuePtr == &CrossPointSettings::pwrBtnFootnoteBack &&
          SETTINGS.shortPwrBtn != CrossPointSettings::SHORT_PWRBTN::FOOTNOTES)
        return StrId::STR_NONE_OPT;
      if ((info.valuePtr == &CrossPointSettings::tenorButtonSymbols ||
           info.valuePtr == &CrossPointSettings::tenorSideArrows) &&
          SETTINGS.uiTheme != CrossPointSettings::TENOR_UI)
        return StrId::STR_NONE_OPT;
      if (SETTINGS.uiTheme == CrossPointSettings::TENOR_UI && info.valuePtr == &CrossPointSettings::statusBarClock)
        return StrId::STR_STATUS_CORNERS;
      return info.nameId;
    }
  }
  if (SETTINGS.uiTheme == CrossPointSettings::TENOR_UI && key == "action/2")
    return StrId::STR_STATUS_CORNERS;
  const auto* item = find(key);
  if (!item) return StrId::STR_NONE_OPT;
  if (SETTINGS.uiTheme == CrossPointSettings::TENOR_UI && strcmp(item->screen, "status") == 0)
    return key == "status/statusBarClock" ? StrId::STR_STATUS_CORNERS : StrId::STR_NONE_OPT;
  if (key == "action/1" && BoardConfig::hasTouch()) return StrId::STR_NONE_OPT;
  return item->label;
}
std::string value(const std::string& key, const std::vector<SettingInfo>& settings) {
  const char* canonical = menucustom::canonicalPinKey(key.c_str());
  if (canonical != key.c_str()) return value(canonical, settings);
  if (unavailableClock(key)) return "";
  const auto* item = find(key);
  if (SETTINGS.uiTheme == CrossPointSettings::TENOR_UI &&
      (key == "status/statusBarClock" || key == "settings/statusBarClock" || key == "action/2")) {
    for (const auto& info : settings) {
      if (info.valuePtr == &CrossPointSettings::statusBarClock)
        return SettingsActivity::settingValueText(buildTenorClockPlacementSetting(info));
    }
  }
  if (item && strcmp(item->screen, "clock") == 0) return DongHoSettingsActivity::giaTriDong(item->row);
  if (item && strcmp(item->screen, "text") == 0) {
    if (item->tab == 2) return TextSettingsActivity::layoutValueText(item->row);
    if (item->tab == 3) return TextSettingsActivity::styleValueText(item->row);
  }
  const auto slash = key.find('/');
  if (slash == std::string::npos) return "";
  for (const auto& info : settings) {
    if (!info.key || key.compare(slash + 1, std::string::npos, info.key) != 0) continue;
    // Favorites never copy credentials or other string values into their labels.
    if (info.type == SettingType::STRING) return "";
    return SettingsActivity::settingValueText(info);
  }
  return "";
}
std::unique_ptr<UiListActivity> open(const std::string& key, GfxRenderer& renderer, MappedInputManager& input) {
  const char* canonical = menucustom::canonicalPinKey(key.c_str());
  if (canonical != key.c_str()) return open(canonical, renderer, input);
  if (unavailableClock(key)) return nullptr;
  const auto* item = find(key);
  std::unique_ptr<UiListActivity> result;
  bool activate = true;
  std::string launchKey = key;
  if (key == "settings/statusBarClock" && SETTINGS.uiTheme != CrossPointSettings::TENOR_UI) {
    result = makeUniqueNoThrow<StatusBarSettingsActivity>(renderer, input);
    launchKey = "status/statusBarClock";
  } else if (key.rfind("settings/", 0) == 0 || key.rfind("action/", 0) == 0) {
    result = makeUniqueNoThrow<SettingsActivity>(renderer, input);
  } else if (!item) {
    return nullptr;
  } else if (strcmp(item->screen, "clock") == 0) {
    result = makeUniqueNoThrow<DongHoSettingsActivity>(renderer, input);
  } else if (strcmp(item->screen, "kosync") == 0) {
    result = makeUniqueNoThrow<KOReaderSettingsActivity>(renderer, input);
  } else if (strcmp(item->screen, "status") == 0) {
    result = makeUniqueNoThrow<StatusBarSettingsActivity>(renderer, input);
  } else if (strcmp(item->screen, "text") == 0) {
    result = makeUniqueNoThrow<TextSettingsActivity>(renderer, input, &sdFontSystem.registry(),
                                                     static_cast<TextSettingsActivity::Tab>(item->tab));
    activate = item->tab >= 2;
  } else if (strcmp(item->screen, "opds") == 0) {
    result = makeUniqueNoThrow<OpdsServerListActivity>(renderer, input);
  }
  if (result) result->launchFavorite(launchKey, activate);
  return result;
}
}  // namespace menufavorites
