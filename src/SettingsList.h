#pragma once

#include <BoardConfig.h>
#include <HalClock.h>
#include <HalTiltSensor.h>
#include <I18n.h>
#include <SdCardFontRegistry.h>

#if defined(TENOR_UI_ACCEPTANCE) && defined(ESP_PLATFORM)
#include <Logging.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

#include <algorithm>
#include <cstring>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "HomeButtonSettings.h"
#include "KOReaderCredentialStore.h"
#include "QuickAction.h"
#include "ReaderFontSizes.h"
#include "shells/ugly/UglyLevel.h"
#include "activities/settings/SettingsActivity.h"
#include "components/UITheme.h"
#include "platform/SimulatorBoardCompat.h"
#include "util/DictionaryRegistry.h"

// Build the font family setting dynamically. When registry is non-null, SD card fonts
// are appended after the built-in fonts. Otherwise only built-in fonts are listed.
inline SettingInfo buildFontFamilySetting(const SdCardFontRegistry* registry) {
  // Built-in font labels (StrId)
  std::vector<StrId> enumValues = {StrId::STR_NOTO_SERIF, StrId::STR_NOTO_SANS};
  // Runtime string labels for SD card fonts
  std::vector<std::string> enumStringValues;

  // Reserve: first CrossPointSettings::BUILTIN_FONT_COUNT entries use StrId, rest use strings
  if (registry) {
    const auto& families = registry->getFamilies();
    enumStringValues.reserve(families.size());
    std::transform(families.begin(), families.end(), std::back_inserter(enumStringValues),
                   [](const SdCardFontFamilyInfo& f) { return f.name; });
  }

  // Capture the SD font count for the lambdas
  const int sdFontCount = static_cast<int>(enumStringValues.size());

  // Total option count = built-in + SD card families
  // For the combined enumStringValues: we need all entries as strings (built-in names + SD names)
  // The render code checks enumStringValues first, then enumValues. So we build enumStringValues
  // with all options when SD fonts are present.
  std::vector<std::string> allStringValues;
  if (sdFontCount > 0) {
    allStringValues.push_back(I18N.get(StrId::STR_NOTO_SERIF));
    allStringValues.push_back(I18N.get(StrId::STR_NOTO_SANS));
    allStringValues.insert(allStringValues.end(), enumStringValues.begin(), enumStringValues.end());
  }

  SettingInfo s;
  s.nameId = StrId::STR_FONT_FAMILY;
  s.type = SettingType::ENUM;
  s.enumValues = std::move(enumValues);
  s.enumStringValues = std::move(allStringValues);
  s.key = "fontFamily";
  s.category = StrId::STR_CAT_READER;
  s.inTextSettings = true;  // matches the static font-family entry it replaces

  // Capture registry families by copy for the lambdas
  std::vector<std::string> sdFamilyNames;
  if (registry) {
    const auto& families = registry->getFamilies();
    sdFamilyNames.reserve(families.size());
    std::transform(families.begin(), families.end(), std::back_inserter(sdFamilyNames),
                   [](const SdCardFontFamilyInfo& f) { return f.name; });
  }

  s.valueGetter = [sdFamilyNames]() -> uint8_t {
    // If an SD card font is selected, find its index
    if (SETTINGS.sdFontFamilyName[0] != '\0') {
      for (int i = 0; i < static_cast<int>(sdFamilyNames.size()); i++) {
        if (sdFamilyNames[i] == SETTINGS.sdFontFamilyName) {
          return static_cast<uint8_t>(CrossPointSettings::BUILTIN_FONT_COUNT + i);
        }
      }
      // SD font name not found in registry - fall through to built-in
    }
    return SETTINGS.fontFamily < CrossPointSettings::BUILTIN_FONT_COUNT ? SETTINGS.fontFamily : 0;
  };

  s.valueSetter = [sdFamilyNames](uint8_t v) {
    if (v < CrossPointSettings::BUILTIN_FONT_COUNT) {
      SETTINGS.fontFamily = v;
      SETTINGS.sdFontFamilyName[0] = '\0';
    } else {
      int sdIdx = v - CrossPointSettings::BUILTIN_FONT_COUNT;
      if (sdIdx < static_cast<int>(sdFamilyNames.size())) {
        strncpy(SETTINGS.sdFontFamilyName, sdFamilyNames[sdIdx].c_str(), sizeof(SETTINGS.sdFontFamilyName) - 1);
        SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
      }
    }
  };

  return s;
}

// Build the font size setting dynamically: the options are the point sizes the
// active family actually ships, so an SD family built at 10/12/14 offers three
// sizes and a family built at 8..18 offers six. The selected point size persists
// in SETTINGS.fontPointSize (saved/loaded manually in CrossPointSettings::
// toJson/fromJson - the generic loop skips dynamic entries), while the ENUM
// contract shared with the web UI stays index-based.
inline SettingInfo buildFontSizeSetting(const SdCardFontRegistry* registry) {
  // Captured by copy: getSettingsList() returns by value and the lambdas outlive
  // this call, so they must not reference the registry.
  const std::vector<uint8_t> sizes = readerFontPointSizes(registry, SETTINGS.sdFontFamilyName);

  // "pt" is deliberately not translated - see the matching note in
  // TextSettingsActivity::rebuildSizeList().
  std::vector<std::string> labels;
  labels.reserve(sizes.size());
  for (const uint8_t pt : sizes) {
    labels.push_back(std::to_string(pt) + " pt");
  }

  SettingInfo s;
  s.nameId = StrId::STR_FONT_SIZE;
  s.type = SettingType::ENUM;
  s.enumStringValues = std::move(labels);
  s.key = "fontSize";
  s.category = StrId::STR_CAT_READER;
  s.inTextSettings = true;  // matches the static font-size entry it replaces

  s.valueGetter = [sizes]() -> uint8_t {
    const uint8_t pt = snapToNearestPointSize(sizes, SETTINGS.fontPointSize);
    for (int i = 0; i < static_cast<int>(sizes.size()); i++) {
      if (sizes[i] == pt) return static_cast<uint8_t>(i);
    }
    return 0;
  };

  s.valueSetter = [sizes](uint8_t v) {
    if (v < sizes.size()) SETTINGS.fontPointSize = sizes[v];
  };

  return s;
}

// Build the dictionary selection setting dynamically from the folders discovered
// under /dictionaries. "None" plus one option per dictionary; the selected folder
// name persists in SETTINGS.dictionaryName (saved/loaded manually in
// CrossPointSettings::toJson/fromJson - the generic loop skips dynamic entries).
inline SettingInfo buildDictionarySetting(const std::vector<DictionaryEntry>& dictionaries) {
  std::vector<std::string> folderNames;
  folderNames.reserve(dictionaries.size());
  std::transform(dictionaries.begin(), dictionaries.end(), std::back_inserter(folderNames),
                 [](const DictionaryEntry& d) { return d.name; });

  SettingInfo s;
  s.nameId = StrId::STR_DICTIONARY;
  s.type = SettingType::ENUM;
  s.enumStringValues.reserve(folderNames.size() + 1);
  s.enumStringValues.push_back(I18N.get(StrId::STR_NONE_OPT));
  s.enumStringValues.insert(s.enumStringValues.end(), folderNames.begin(), folderNames.end());
  s.category = StrId::STR_CAT_READER;

  s.valueGetter = [folderNames]() -> uint8_t {
    for (size_t i = 0; i < folderNames.size(); i++) {
      // Compare within the settings field capacity: an over-long folder name is
      // stored truncated, and must still match its list entry.
      if (strncmp(folderNames[i].c_str(), SETTINGS.dictionaryName, sizeof(SETTINGS.dictionaryName) - 1) == 0) {
        return static_cast<uint8_t>(i + 1);
      }
    }
    return 0;  // "None", also when the stored folder no longer exists
  };

  s.valueSetter = [folderNames](uint8_t v) {
    if (v == 0 || v > folderNames.size()) {
      SETTINGS.dictionaryName[0] = '\0';
      return;
    }
    strncpy(SETTINGS.dictionaryName, folderNames[v - 1].c_str(), sizeof(SETTINGS.dictionaryName) - 1);
    SETTINGS.dictionaryName[sizeof(SETTINGS.dictionaryName) - 1] = '\0';
  };

  return s;
}

// Hold-Select row. The list shows the functions this board offers, in stored-number order; what is saved is
// the number (CrossPointSettings.cpp saves and loads it by hand, like the dictionary). A board without an IMU
// leaves the tilt toggle out, so there Save quotation sits at list position 6 and is still stored as 7.
inline SettingInfo buildLongPressMenuSetting(const bool hasTilt) {
  static constexpr StrId LABELS[] = {StrId::STR_KOSYNC,     StrId::STR_DISABLED,        StrId::STR_BOOKMARK_OPTION,
                                     StrId::STR_DICTIONARY, StrId::STR_READER_MENU,     StrId::STR_FILE_TRANSFER,
                                     StrId::STR_TILT_PAGE_TURN, StrId::STR_QUOTES_SAVE_ACTION,
                                     StrId::STR_BLE_CONNECT_REMOTE};
  static_assert(std::size(LABELS) == CrossPointSettings::LONG_PRESS_MENU_FUNCTION_COUNT, "one label per function");
  std::vector<StrId> offered;
  for (uint8_t number = 0; number < std::size(LABELS); ++number) {
#if !FREEINK_CAP_BLE_HID_HOST
    if (number == CrossPointSettings::LP_MENU_CONNECT_REMOTE) continue;
#endif
    if (hasTilt || number != CrossPointSettings::LP_MENU_TILT_PAGE_TURN) offered.push_back(LABELS[number]);
  }
  const uint8_t skipped = hasTilt ? 0 : 1;  // list positions after the tilt number sit one lower
  return SettingInfo::DynamicEnum(
      StrId::STR_LONG_PRESS_MENU, std::move(offered),
      [skipped]() -> uint8_t {
        const uint8_t number = SETTINGS.longPressMenuFunction;
        return number > CrossPointSettings::LP_MENU_TILT_PAGE_TURN ? number - skipped : number;
      },
      [skipped](const uint8_t position) {
        SETTINGS.longPressMenuFunction =
            position >= CrossPointSettings::LP_MENU_TILT_PAGE_TURN ? position + skipped : position;
      },
      "longPressMenuFunction", StrId::STR_CAT_CONTROLS);
}

// Tenor shows the two visible corner layouts. Legacy value 0 reads as right,
// matching the renderer, and is preserved until an explicit selection is made.
inline SettingInfo buildTenorClockPlacementSetting(const SettingInfo& registered) {
  SettingInfo setting = registered;
  setting.nameId = StrId::STR_STATUS_CORNERS;
  setting.category = StrId::STR_CAT_DISPLAY;
  setting.valuePtr = nullptr;
  setting.enumValues = {StrId::STR_BATTERY_LEFT_CLOCK_RIGHT, StrId::STR_CLOCK_LEFT_BATTERY_RIGHT};
  setting.valueGetter = []() -> uint8_t {
    return SETTINGS.statusBarClock == CrossPointSettings::STATUS_BAR_CLOCK_LEFT ? 1 : 0;
  };
  setting.valueSetter = [](uint8_t value) {
    SETTINGS.statusBarClock = value == 1 ? CrossPointSettings::STATUS_BAR_CLOCK_LEFT
                                         : CrossPointSettings::STATUS_BAR_CLOCK_RIGHT;
  };
  return setting;
}

// Shared settings list used by both the device settings UI and the web settings API.
// Each entry has a key (for JSON API) and category (for grouping).
// ACTION-type entries and entries without a key are device-only.
//
// Cached descriptors keep member pointers and settings-field offsets. Values are
// read on demand, so save/load can iterate by reference without allocating a
// second catalog. UI consumers copy only the rows they own.
namespace settings_catalog {
inline std::vector<SettingInfo>& storage() {
  static std::vector<SettingInfo> rows;
  return rows;
}
}  // namespace settings_catalog

// Call only at a quiescent activity transition under RenderLock, after all
// borrowed descriptors/iterators have left scope. Owned category copies survive.
// Any later getter, including settings persistence, rebuilds the full catalog.
inline void releaseBaseSettingsList() {
  std::vector<SettingInfo>().swap(settings_catalog::storage());
}

// These rows also appear over the saved reader page. Build only the requested
// descriptor, with static labels, so the lookup works even with an exhausted heap.
inline std::optional<SettingInfo> getBaseTextSetting(const char* key) {
  if (!key) return std::nullopt;
  static constexpr StrId spacing[] = {StrId::STR_INK_DEFAULT, StrId::STR_VERY_NARROW, StrId::STR_TIGHT,
                                      StrId::STR_WIDE, StrId::STR_VERY_WIDE};
  static constexpr StrId indent[] = {StrId::STR_STATE_OFF, StrId::STR_INK_DEFAULT, StrId::STR_WIDE};
  static constexpr StrId ink[] = {StrId::STR_READER_INK_0, StrId::STR_READER_INK_1,
                                  StrId::STR_READER_INK_2, StrId::STR_READER_INK_3,
                                  StrId::STR_READER_INK_4, StrId::STR_READER_INK_5};
  SettingInfo row;
  if (strcmp(key, "letterSpacing") == 0)
    row = SettingInfo::StaticEnum(StrId::STR_LETTER_SPACING, &CrossPointSettings::letterSpacing, spacing,
                                  "letterSpacing", StrId::STR_CAT_READER);
  else if (strcmp(key, "wordSpacing") == 0)
    row = SettingInfo::StaticEnum(StrId::STR_WORD_SPACING, &CrossPointSettings::wordSpacing, spacing,
                                  "wordSpacing", StrId::STR_CAT_READER);
  else if (strcmp(key, "extraParagraphSpacing") == 0)
    row = SettingInfo::StaticEnum(StrId::STR_EXTRA_SPACING, &CrossPointSettings::extraParagraphSpacing, spacing,
                                  "extraParagraphSpacing", StrId::STR_CAT_READER);
  else if (strcmp(key, "screenMargin") == 0)
    row = SettingInfo::Value(StrId::STR_SCREEN_MARGIN, &CrossPointSettings::screenMargin,
                             {CrossPointSettings::SCREEN_MARGIN_MIN, CrossPointSettings::SCREEN_MARGIN_MAX,
                              CrossPointSettings::SCREEN_MARGIN_STEP}, "screenMargin", StrId::STR_CAT_READER);
  else if (strcmp(key, "paragraphIndent") == 0)
    row = SettingInfo::StaticEnum(StrId::STR_PARAGRAPH_INDENT, &CrossPointSettings::paragraphIndent, indent,
                                  "paragraphIndent", StrId::STR_CAT_READER);
  else if (strcmp(key, "embeddedStyle") == 0)
    row = SettingInfo::Toggle(StrId::STR_EMBEDDED_STYLE, &CrossPointSettings::embeddedStyle,
                              "embeddedStyle", StrId::STR_CAT_READER);
  else if (strcmp(key, "hyphenationEnabled") == 0)
    row = SettingInfo::Toggle(StrId::STR_HYPHENATION, &CrossPointSettings::hyphenationEnabled,
                              "hyphenationEnabled", StrId::STR_CAT_READER);
  else if (strcmp(key, "readerInkWeight") == 0)
    row = SettingInfo::StaticEnum(StrId::STR_READER_INK_WEIGHT, &CrossPointSettings::readerInkWeight, ink,
                                  "readerInkWeight", StrId::STR_CAT_READER);
  else if (strcmp(key, "textAntiAliasing") == 0)
    row = SettingInfo::Toggle(StrId::STR_TEXT_AA, &CrossPointSettings::textAntiAliasing,
                              "textAntiAliasing", StrId::STR_CAT_READER);
  else
    return std::nullopt;
  row.inTextSettings = true;
  return row;
}

// Every base descriptor, built one at a time and handed to emit (SettingInfo&&). Settings save and load
// walk it here instead of keeping the whole catalog (~15 KB on the X3) for the life of the program.
// One body for every caller: a template copied the 100 rows' code into each (28 KB of flash on x3-ble).
inline void forEachBaseSetting(const std::function<void(SettingInfo&&)>& emit) {
    // Enum settings are persisted as numeric values. Assign these labels by enum
    // value so a reordered menu or enum cannot silently swap their behavior.
    std::vector<StrId> sleepScreenValues(CrossPointSettings::SLEEP_SCREEN_MODE_COUNT);
    sleepScreenValues[CrossPointSettings::DARK] = StrId::STR_DARK;
    sleepScreenValues[CrossPointSettings::LIGHT] = StrId::STR_LIGHT;
    sleepScreenValues[CrossPointSettings::CUSTOM] = StrId::STR_CUSTOM;
    sleepScreenValues[CrossPointSettings::COVER] = StrId::STR_COVER;
    sleepScreenValues[CrossPointSettings::COVER_CUSTOM] = StrId::STR_COVER_CUSTOM;
    sleepScreenValues[CrossPointSettings::BLANK] = StrId::STR_NONE_OPT;
    sleepScreenValues[CrossPointSettings::QUICK_RESUME] = StrId::STR_QUICK_RESUME;
    sleepScreenValues[CrossPointSettings::TRANSPARENT_CUSTOM] = StrId::STR_TRANSPARENT;
    sleepScreenValues[CrossPointSettings::TENOR] = StrId::STR_SLEEP_TENOR;
    sleepScreenValues[CrossPointSettings::STATS] = StrId::STR_SLEEP_STATS;
    sleepScreenValues[CrossPointSettings::QUOTE] = StrId::STR_SLEEP_QUOTE;
    sleepScreenValues[CrossPointSettings::UGLY] = StrId::STR_SLEEP_UGLY;

    std::vector<StrId> statusBarClockValues(CrossPointSettings::STATUS_BAR_CLOCK_MODE_COUNT);
    statusBarClockValues[CrossPointSettings::STATUS_BAR_CLOCK_HIDE] = StrId::STR_HIDE;
    statusBarClockValues[CrossPointSettings::STATUS_BAR_CLOCK_RIGHT] = StrId::STR_DIR_RIGHT;
    statusBarClockValues[CrossPointSettings::STATUS_BAR_CLOCK_LEFT] = StrId::STR_DIR_LEFT;

    const bool hasTilt = halTiltSensor.isAvailable();
    // --- Display ---
    emit(SettingInfo::Enum(StrId::STR_UI_TEXT_SIZE, &CrossPointSettings::uiTextSize,
                          {StrId::STR_UI_SIZE_SMALL, StrId::STR_UI_SIZE_MEDIUM, StrId::STR_UI_SIZE_LARGE},
                          "uiTextSize", StrId::STR_CAT_DISPLAY));
    emit(SettingInfo::Enum(StrId::STR_HIDE_GLOBAL_STATUS_BAR, &CrossPointSettings::globalStatusBarMode,
                          {StrId::STR_STATUS_BAR_SMALL, StrId::STR_STATE_OFF, StrId::STR_STATUS_BAR_LARGE},
                          "globalStatusBarMode", StrId::STR_CAT_DISPLAY));
    emit(SettingInfo::Toggle(StrId::STR_SIDE_ARROW_HINTS, &CrossPointSettings::tenorSideArrows, "tenorSideArrows",
                            StrId::STR_CAT_DISPLAY));
    emit(SettingInfo::Enum(StrId::STR_BUTTON_LABELS, &CrossPointSettings::tenorButtonSymbols,
                          {StrId::STR_LABELS_TEXT, StrId::STR_LABELS_SYMBOLS}, "tenorButtonSymbols",
                          StrId::STR_CAT_DISPLAY));
    emit(SettingInfo::Enum(StrId::STR_HIDE_BATTERY, &CrossPointSettings::hideBatteryPercentage,
                          {StrId::STR_NEVER, StrId::STR_IN_READER, StrId::STR_ALWAYS}, "hideBatteryPercentage",
                          StrId::STR_CAT_DISPLAY));
    emit(SettingInfo::Enum(StrId::STR_REFRESH_FREQ, &CrossPointSettings::refreshFrequency,
                          {StrId::STR_PAGES_1, StrId::STR_PAGES_5, StrId::STR_PAGES_10, StrId::STR_PAGES_15,
                           StrId::STR_PAGES_30, StrId::STR_NEVER},
                          "refreshFrequency", StrId::STR_CAT_DISPLAY));
    emit(SettingInfo::Toggle(StrId::STR_SUNLIGHT_FADING_FIX, &CrossPointSettings::fadingFix, "fadingFix",
                            StrId::STR_CAT_DISPLAY));
    // Just above night mode, so the first rows and the last row of the screen group keep their place. The shell is chosen at run time: 0 tenor/cross (what every earlier release shows), 1 tenor/ugly.
    emit(SettingInfo::Enum(StrId::STR_UI_SHELL, &CrossPointSettings::uiShell,
                          {StrId::STR_SHELL_CROSS, StrId::STR_SHELL_UGLY}, "uiShell", StrId::STR_CAT_DISPLAY));
    // Right under the shell. The screen lists it only while the shell is tenor/ugly (SettingsActivity), the saved file keeps it always.
    emit(SettingInfo::Enum(StrId::STR_SHELL_LEVEL, &CrossPointSettings::uiUglyLevel,
                          {StrId::STR_SHELL_LEVEL_PLAIN, StrId::STR_SHELL_LEVEL_AF}, "uiUglyLevel", StrId::STR_CAT_DISPLAY));
    emit(SettingInfo::Toggle(StrId::STR_NIGHT_MODE, &CrossPointSettings::screenInverted, "screenInverted",
                            StrId::STR_CAT_DISPLAY));
    emit(SettingInfo::Enum(StrId::STR_SLEEP_SCREEN, &CrossPointSettings::sleepScreen, std::move(sleepScreenValues),
                          "sleepScreen", StrId::STR_CAT_DISPLAY));
    emit(SettingInfo::Enum(StrId::STR_SLEEP_COVER_MODE, &CrossPointSettings::sleepScreenCoverMode,
                          {StrId::STR_FIT, StrId::STR_CROP}, "sleepScreenCoverMode", StrId::STR_CAT_DISPLAY));
    emit(SettingInfo::Enum(StrId::STR_SLEEP_COVER_FILTER, &CrossPointSettings::sleepScreenCoverFilter,
                          {StrId::STR_NONE_OPT, StrId::STR_FILTER_CONTRAST, StrId::STR_INVERTED},
                          "sleepScreenCoverFilter", StrId::STR_CAT_DISPLAY));
    emit(SettingInfo::Enum(StrId::STR_QUICK_RESUME_TIMEOUT, &CrossPointSettings::quickResumeSleepScreen,
                          {StrId::STR_STATE_OFF, StrId::STR_STATE_ON}, "quickResumeSleepScreen",
                          StrId::STR_CAT_DISPLAY));
    emit(SettingInfo::Enum(StrId::STR_WAKE_INTO_BOOK, &CrossPointSettings::wakeIntoBook,
                          {StrId::STR_STATE_OFF, StrId::STR_STATE_ON}, "wakeIntoBook", StrId::STR_CAT_DISPLAY));
    emit(SettingInfo::Toggle(StrId::STR_WAKE_NOTICE, &CrossPointSettings::wakeNotice, "wakeNotice",
                                    StrId::STR_CAT_DISPLAY));
    emit(SettingInfo::Toggle(StrId::STR_SLEEP_BW_REFRESH, &CrossPointSettings::sleepBwFold, "sleepBwFold",
                                    StrId::STR_CAT_DISPLAY));
#if FREEINK_CAP_FRONTLIGHT
    emit(SettingInfo::Toggle(StrId::STR_RESTORE_LIGHT_ON_WAKE, &CrossPointSettings::frontlightRestoreOnWake,
                            "frontlightRestoreOnWake", StrId::STR_CAT_DISPLAY));
#endif

    // --- Reader ---
    // Built-in font-family entry. Replaced per-call with a registry-aware
    // version when SD fonts are installed.
    emit(std::move(SettingInfo::Enum(StrId::STR_FONT_FAMILY, &CrossPointSettings::fontFamily,
                          {StrId::STR_NOTO_SERIF, StrId::STR_NOTO_SANS}, "fontFamily", StrId::STR_CAT_READER)
            .withTextSettings()));
    // Placeholder: the selectable sizes depend on the active font family, so
    // this entry is always replaced by buildFontSizeSetting() below. It only
    // fixes the setting's position in the Reader category.
    emit(std::move(SettingInfo::Enum(StrId::STR_FONT_SIZE, nullptr, {}, "fontSize", StrId::STR_CAT_READER).withTextSettings()));
    // Layout group, in the order the approved plan fixes: the four spacing
    // kinds first, then alignment, margin and paragraph indent. The style
    // group (embedded style, drop cap, hyphenation, ink weight, AA) follows.
    emit(std::move(SettingInfo::Enum(StrId::STR_LINE_SPACING, &CrossPointSettings::lineSpacing,
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
                          {StrId::STR_INK_DEFAULT, StrId::STR_VERY_NARROW, StrId::STR_TIGHT, StrId::STR_WIDE,
                           StrId::STR_VERY_WIDE, StrId::STR_LINE_SPACING_130, StrId::STR_LINE_SPACING_160},
#else
                          {StrId::STR_INK_DEFAULT, StrId::STR_VERY_NARROW, StrId::STR_TIGHT, StrId::STR_WIDE,
                           StrId::STR_VERY_WIDE},
#endif
                          "lineSpacing", StrId::STR_CAT_READER)
            .withTextSettings()));
    emit(std::move(*getBaseTextSetting("letterSpacing")));
    emit(std::move(*getBaseTextSetting("wordSpacing")));
    emit(std::move(*getBaseTextSetting("extraParagraphSpacing")));
    emit(std::move(SettingInfo::Enum(StrId::STR_PARA_ALIGNMENT, &CrossPointSettings::paragraphAlignment,
                          {StrId::STR_JUSTIFY, StrId::STR_ALIGN_LEFT, StrId::STR_CENTER, StrId::STR_ALIGN_RIGHT,
                           StrId::STR_BOOK_S_STYLE},
                          "paragraphAlignment", StrId::STR_CAT_READER)
            .withTextSettings()));
    emit(std::move(*getBaseTextSetting("screenMargin")));
    emit(std::move(*getBaseTextSetting("paragraphIndent")));
    emit(std::move(*getBaseTextSetting("embeddedStyle")));
    emit(std::move(SettingInfo::Enum(StrId::STR_FOCUS_READING, &CrossPointSettings::dropCapMode,
                          {StrId::STR_STATE_OFF, StrId::STR_INK_DEFAULT, StrId::STR_SPACING_LARGE}, "dropCapMode",
                          StrId::STR_CAT_READER)
            .withTextSettings()));
    emit(std::move(*getBaseTextSetting("hyphenationEnabled")));
    emit(SettingInfo::Enum(
            StrId::STR_ORIENTATION, &CrossPointSettings::orientation,
            {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW, StrId::STR_ORIENTATION_INVERTED, StrId::STR_LANDSCAPE_CCW},
            "orientation", StrId::STR_CAT_READER));

    emit(std::move(*getBaseTextSetting("readerInkWeight")));
    emit(std::move(*getBaseTextSetting("textAntiAliasing")));
    emit(SettingInfo::Enum(StrId::STR_IMAGES, &CrossPointSettings::imageRendering,
                          {StrId::STR_IMAGES_DISPLAY, StrId::STR_IMAGES_PLACEHOLDER, StrId::STR_IMAGES_SUPPRESS},
                          "imageRendering", StrId::STR_CAT_READER));
    // Thanh trang thai trong trinh doc: sau muc, doc lap voi thanh ngoai.
    emit(SettingInfo::Enum(StrId::STR_HIDE_READER_STATUS_BAR, &CrossPointSettings::readerStatusBarMode,
                          {StrId::STR_STATE_OFF, StrId::STR_STATUS_BAR_CLOCK_BATTERY, StrId::STR_STATUS_BAR_DEFAULT,
                           StrId::STR_STATUS_BAR_CHAPTER_PROGRESS, StrId::STR_STATUS_BAR_CHAPTER_CLOCK,
                           StrId::STR_STATUS_BAR_CHAPTER_BATTERY, StrId::STR_STATUS_BAR_CLOCK_CHAPTER_PROGRESS,
                           StrId::STR_STATUS_BAR_BOOK_DETAILS},
                          "readerStatusBarMode", StrId::STR_CAT_READER));
    emit(SettingInfo::Enum(StrId::STR_SIDE_BTN_LAYOUT, &CrossPointSettings::sideButtonLayout,
                          {StrId::STR_PREV_NEXT, StrId::STR_NEXT_PREV, StrId::STR_DISABLED, StrId::STR_NEXT_NEXT,
                           StrId::STR_PREV_PREV},
                          "sideButtonLayout", StrId::STR_CAT_READER));
    emit(SettingInfo::Enum(StrId::STR_READER_MENU_STYLE, &CrossPointSettings::readerMenuStyle,
                          {StrId::STR_MENU_STYLE_LIST, StrId::STR_MENU_STYLE_TOOLBAR}, "readerMenuStyle",
                          StrId::STR_CAT_READER));
    // --- Controls ---
    // touchReaderControls is a master toggle (#3586); direction gestures are
    // separate pageTurnGesture/previousPageGesture settings. All three are
    // touch-only (settingHiddenOnThisBoard hides them without a touch controller).
    emit(SettingInfo::Toggle(StrId::STR_TOUCH_READER_CONTROLS, &CrossPointSettings::touchReaderControls,
                            "touchReaderControls", StrId::STR_CAT_CONTROLS));
    emit(SettingInfo::Enum(StrId::STR_NEXT_PAGE_GESTURE, &CrossPointSettings::pageTurnGesture,
                          {StrId::STR_TAP_AND_SWIPE, StrId::STR_TAP_ONLY, StrId::STR_SWIPE_ONLY,
                           StrId::STR_INVERTED_TAP, StrId::STR_DISABLED},
                          "pageTurnGesture", StrId::STR_CAT_CONTROLS));
    emit(SettingInfo::Enum(StrId::STR_PREV_PAGE_GESTURE, &CrossPointSettings::previousPageGesture,
                          {StrId::STR_TAP_AND_SWIPE, StrId::STR_TAP_ONLY, StrId::STR_SWIPE_ONLY,
                           StrId::STR_INVERTED_TAP, StrId::STR_DISABLED},
                          "previousPageGesture", StrId::STR_CAT_CONTROLS));
    emit(SettingInfo::Enum(StrId::STR_BACK_TAP_ZONE, &CrossPointSettings::backTapZone,
                          {StrId::STR_PERCENT_15, StrId::STR_PERCENT_20, StrId::STR_PERCENT_25, StrId::STR_PERCENT_33},
                          "backTapZone", StrId::STR_CAT_CONTROLS));
    emit(SettingInfo::Toggle(StrId::STR_READER_TAP_TIP, &CrossPointSettings::readerTapTip, "readerTapTip",
                                    StrId::STR_CAT_CONTROLS));
    // Persisted under the legacy "tapForReaderMenu" key: old saves map
    // 0 = Off, 1 = Tap.
    emit(SettingInfo::Enum(StrId::STR_SHOW_READER_MENU, &CrossPointSettings::showReaderMenu,
                          {StrId::STR_STATE_OFF, StrId::STR_STATE_TAP, StrId::STR_STATE_SWIPE_UP}, "tapForReaderMenu",
                          StrId::STR_CAT_CONTROLS));
    emit(SettingInfo::Toggle(StrId::STR_FRONT_BTN_FOLLOW_ORIENTATION, &CrossPointSettings::frontButtonFollowOrientation,
                            "frontButtonFollowOrientation", StrId::STR_CAT_CONTROLS));
    emit(SettingInfo::Enum(StrId::STR_KEYBOARD_GEOMETRY, &CrossPointSettings::keyboardAligned,
                          {StrId::STR_KEYBOARD_STAGGERED, StrId::STR_KEYBOARD_ALIGNED}, "keyboardAligned",
                          StrId::STR_CAT_KEYBOARD));
    emit(SettingInfo::Toggle(StrId::STR_KEYBOARD_AXIS_SWAP, &CrossPointSettings::keyboardAxisSwapped,
                            "keyboardAxisSwapped", StrId::STR_CAT_KEYBOARD));
    emit(SettingInfo::String(StrId::STR_DEVICE_NAME, &SETTINGS.deviceName[0], sizeof(SETTINGS.deviceName), "deviceName"));
    emit(SettingInfo::Enum(StrId::STR_LONG_PRESS_BEHAVIOR, &CrossPointSettings::longPressButtonBehavior,
                          {StrId::STR_LONG_PRESS_BEHAVIOR_OFF, StrId::STR_LONG_PRESS_BEHAVIOR_SKIP,
                           StrId::STR_LONG_PRESS_BEHAVIOR_ORIENTATION},
                          "longPressButtonBehavior", StrId::STR_CAT_CONTROLS));
    emit(buildLongPressMenuSetting(hasTilt));
    // X4 Pro only; hidden elsewhere by settingHiddenOnThisBoard (#3089).
    emit(SettingInfo::Toggle(StrId::STR_DBL_CLICK_PWR_LIGHT, &CrossPointSettings::doubleClickPwrLight,
                            "doubleClickPwrLight", StrId::STR_CAT_CONTROLS));
    // Short power press and hard shake share one list of actions (QuickAction.h).
    emit(SettingInfo::Enum(StrId::STR_SHORT_PWR_BTN, &CrossPointSettings::shortPwrBtn,
                          quickaction::powerLabels(), "shortPwrBtn", StrId::STR_CAT_CONTROLS));
    emit(SettingInfo::Toggle(StrId::STR_PWR_BTN_FOOTNOTE_BACK, &CrossPointSettings::pwrBtnFootnoteBack,
                            "pwrBtnFootnoteBack", StrId::STR_CAT_CONTROLS));
    emit(SettingInfo::Toggle(StrId::STR_BACK_SHORT_TO_FILE_BROWSER, &CrossPointSettings::backShortToFileBrowser,
                            "backShortToFileBrowser", StrId::STR_CAT_CONTROLS));
    // Home button shortcuts (#3516): tap/double-tap/long-press, home-key boards only.
    for (unsigned i = 0; i < 3; ++i) {
      emit(SettingInfo::StaticEnum(home_button::GESTURE_LABELS[i], home_button::FIELDS[i],
                                          home_button::ACTION_LABELS, home_button::KEYS[i], StrId::STR_CAT_CONTROLS));
    }
    // --- Gestures (only with the QMI8658 IMU, X3) ---
    // Keys and values are the ones these rows had in Reader and Controls, so a saved
    // file reads the same here.
    if (hasTilt) {
      emit(SettingInfo::Enum(StrId::STR_TILT_PAGE_TURN, &CrossPointSettings::tiltPageTurn,
                            // STR_INVERTED means inverted colours elsewhere; tilt needs a
                            // reversed direction, so it gets a word of its own.
                            {StrId::STR_STATE_OFF, StrId::STR_NORMAL, StrId::STR_TILT_INVERTED}, "tiltPageTurn",
                            StrId::STR_CAT_MOTION));
      emit(SettingInfo::Enum(StrId::STR_TILT_TAB_NAVIGATION, &CrossPointSettings::tiltTabNavigation,
                            {StrId::STR_STATE_OFF, StrId::STR_NORMAL, StrId::STR_TILT_INVERTED},
                            "tiltTabNavigation", StrId::STR_CAT_MOTION));
      // Row tilt sits next to tab tilt: same band of gestures, other axis.
      emit(SettingInfo::Enum(StrId::STR_TILT_MENU_NAVIGATION, &CrossPointSettings::tiltMenuNavigation,
                            {StrId::STR_STATE_OFF, StrId::STR_NORMAL, StrId::STR_TILT_INVERTED},
                            "tiltMenuNavigation", StrId::STR_CAT_MOTION));
      // Flick strength per axis: side flicks turn pages and tabs, up/down
      // flicks move menu rows, and wrists differ on each.
      emit(SettingInfo::Enum(StrId::STR_TILT_STRENGTH_H, &CrossPointSettings::tiltStrengthH,
                            {StrId::STR_TILT_LIGHT, StrId::STR_UI_SIZE_MEDIUM, StrId::STR_TILT_STRONG},
                            "tiltStrengthH", StrId::STR_CAT_MOTION));
      emit(SettingInfo::Enum(StrId::STR_TILT_STRENGTH_V, &CrossPointSettings::tiltStrengthV,
                            {StrId::STR_TILT_LIGHT, StrId::STR_UI_SIZE_MEDIUM, StrId::STR_TILT_STRONG},
                            "tiltStrengthV", StrId::STR_CAT_MOTION));
      // A hard shake, face down, face up and a double tap each run one of the power
      // button's actions on any screen, chosen from the shake's list (QuickAction.h).
      emit(SettingInfo::Enum(StrId::STR_SHAKE_ACTION, &CrossPointSettings::shakeAction,
                            quickaction::shakeLabels(), "shakeAction", StrId::STR_CAT_MOTION));
      emit(SettingInfo::Enum(StrId::STR_SHAKE_STRENGTH, &CrossPointSettings::shakeStrength,
                            {StrId::STR_TILT_LIGHT, StrId::STR_UI_SIZE_MEDIUM, StrId::STR_TILT_STRONG},
                            "shakeStrength", StrId::STR_CAT_MOTION));
      emit(SettingInfo::Enum(StrId::STR_FACE_DOWN_ACTION, &CrossPointSettings::faceDownAction,
                            quickaction::shakeLabels(), "faceDownAction", StrId::STR_CAT_MOTION));
      emit(SettingInfo::Enum(StrId::STR_FACE_UP_ACTION, &CrossPointSettings::faceUpAction,
                            quickaction::shakeLabels(), "faceUpAction", StrId::STR_CAT_MOTION));
      emit(SettingInfo::Enum(StrId::STR_DOUBLE_TAP_ACTION, &CrossPointSettings::doubleTapAction,
                            quickaction::shakeLabels(), "doubleTapAction", StrId::STR_CAT_MOTION));
      emit(SettingInfo::Enum(StrId::STR_SCREEN_TAP_ACTION, &CrossPointSettings::screenTapAction,
                            quickaction::shakeLabels(), "screenTapAction", StrId::STR_CAT_MOTION));
      emit(SettingInfo::Enum(StrId::STR_EDGE_TAP_ACTION, &CrossPointSettings::edgeTapAction,
                            quickaction::shakeLabels(), "edgeTapAction", StrId::STR_CAT_MOTION));
    }

    // --- System ---
    emit(SettingInfo::Value(
            StrId::STR_TIME_TO_SLEEP, &CrossPointSettings::sleepTimeoutMinutes,
            {CrossPointSettings::MIN_SLEEP_TIMEOUT_MINUTES, CrossPointSettings::MAX_SLEEP_TIMEOUT_MINUTES, 1},
            "sleepTimeoutMinutes", StrId::STR_CAT_SYSTEM));
    emit(SettingInfo::Toggle(StrId::STR_SHOW_HIDDEN_FILES, &CrossPointSettings::showHiddenFiles, "showHiddenFiles",
                            StrId::STR_CAT_SYSTEM));
    emit(SettingInfo::Toggle(StrId::STR_LIBRARY_USE_METADATA, &CrossPointSettings::libraryUseMetadata,
                            "libraryUseMetadata", StrId::STR_CAT_SYSTEM));
    emit(SettingInfo::Toggle(StrId::STR_REMOVE_READ_FROM_RECENTS, &CrossPointSettings::removeReadBooksFromRecents,
                            "removeReadBooksFromRecents", StrId::STR_CAT_SYSTEM));
    emit(SettingInfo::Toggle(StrId::STR_MOVE_FINISHED_TO_READ, &CrossPointSettings::moveFinishedToReadFolder,
                            "moveFinishedToReadFolder", StrId::STR_CAT_SYSTEM));

    // OPDS download folder: persisted + web-exposed, but category-less so it
    // is hidden from the on-device Settings screen (edited via OPDS UI).
    emit(SettingInfo::String(StrId::STR_OPDS_DOWNLOAD_FOLDER, &SETTINGS.opdsDownloadFolder[0],
                            sizeof(SETTINGS.opdsDownloadFolder), "opdsDownloadFolder"));
    // OPDS download filename format: persisted + web-exposed, category-less so it
    // is hidden from the on-device Settings screen (cycled from the OPDS UI).
    emit(SettingInfo::Enum(StrId::STR_OPDS_FILENAME_FORMAT, &CrossPointSettings::opdsFilenameFormat,
                          {StrId::STR_FMT_AUTHOR_TITLE, StrId::STR_FMT_TITLE_AUTHOR, StrId::STR_FMT_TITLE},
                          "opdsFilenameFormat"));

    // The sleep screen tenor/ugly took over: persisted only, category-less (so not on the device) and kept
    // off the web page by getSettingsList().
    emit(SettingInfo::Value(StrId::STR_UI_SHELL, &CrossPointSettings::uiShellSleepMemo,
                           {0, CrossPointSettings::SLEEP_SCREEN_MODE_COUNT, 1}, "uiShellSleepMemo"));
    emit(SettingInfo::Toggle(StrId::STR_UI_SHELL, &CrossPointSettings::uiShellClockMemo, "uiShellClockMemo"));
    emit(SettingInfo::Toggle(StrId::STR_UI_SHELL, &CrossPointSettings::uiShellClockOnce, "uiShellClockOnce"));
    emit(SettingInfo::Toggle(StrId::STR_UI_SHELL, &CrossPointSettings::uglyBatteryHidden, "uglyBatteryHidden"));

    // Frontlight quick-panel state: persisted and web-exposed, but hidden
    // from the on-device Settings screen because the swipe panel owns it.
    emit(SettingInfo::Value(StrId::STR_BRIGHTNESS, &CrossPointSettings::frontlightBrightness, {0, 100, 5},
                           "frontlightBrightness"));
#if FREEINK_CAP_WARMLIGHT
    emit(SettingInfo::Value(StrId::STR_WARMTH, &CrossPointSettings::frontlightWarmth, {0, 100, 5}, "frontlightWarmth"));
#endif
    emit(SettingInfo::Toggle(StrId::STR_FRONTLIGHT, &CrossPointSettings::frontlightOn, "frontlightOn"));

    // --- KOReader Sync (web-only, uses KOReaderCredentialStore) ---
    emit(SettingInfo::DynamicString(
            StrId::STR_KOREADER_USERNAME, [] { return KOREADER_STORE.getUsername(); },
            [](const std::string& v) {
              KOREADER_STORE.setCredentials(v, KOREADER_STORE.getPassword());
              KOREADER_STORE.saveToFile();
            },
            "koUsername", StrId::STR_KOREADER_SYNC));
    emit(SettingInfo::DynamicString(
            StrId::STR_KOREADER_PASSWORD, [] { return KOREADER_STORE.getPassword(); },
            [](const std::string& v) {
              KOREADER_STORE.setCredentials(KOREADER_STORE.getUsername(), v);
              KOREADER_STORE.saveToFile();
            },
            "koPassword", StrId::STR_KOREADER_SYNC));
    emit(SettingInfo::DynamicString(
            StrId::STR_SYNC_SERVER_URL, [] { return KOREADER_STORE.getServerUrl(); },
            [](const std::string& v) {
              KOREADER_STORE.setServerUrl(v);
              KOREADER_STORE.saveToFile();
            },
            "koServerUrl", StrId::STR_KOREADER_SYNC));
    emit(SettingInfo::DynamicEnum(
            StrId::STR_DOCUMENT_MATCHING, {StrId::STR_FILENAME, StrId::STR_BINARY},
            [] { return static_cast<uint8_t>(KOREADER_STORE.getMatchMethod()); },
            [](uint8_t v) {
              KOREADER_STORE.setMatchMethod(static_cast<DocumentMatchMethod>(v));
              KOREADER_STORE.saveToFile();
            },
            "koMatchMethod", StrId::STR_KOREADER_SYNC));
    emit(SettingInfo::DynamicEnum(
            StrId::STR_SEND_METADATA, {StrId::STR_STATE_OFF, StrId::STR_STATE_ON},
            [] { return static_cast<uint8_t>(KOREADER_STORE.getSendMetadata()); },
            [](uint8_t v) {
              KOREADER_STORE.setSendMetadata(v != 0);
              KOREADER_STORE.saveToFile();
            },
            "koSendMetadata", StrId::STR_KOREADER_SYNC));
    emit(SettingInfo::DynamicEnum(
            StrId::STR_SYNC_BEHAVIOR, {StrId::STR_ASK_EVERY_TIME, StrId::STR_SMART_SYNC},
            [] { return static_cast<uint8_t>(KOREADER_STORE.getSyncBehavior()); },
            [](uint8_t v) {
              KOREADER_STORE.setSyncBehavior(static_cast<KOReaderSyncBehavior>(v));
              KOREADER_STORE.saveToFile();
            },
            "koSyncBehavior", StrId::STR_KOREADER_SYNC));
    // --- Status Bar Settings (web-only, uses StatusBarSettingsActivity) ---
    emit(SettingInfo::DynamicEnum(StrId::STR_READER_STATUS_TOP,
          {StrId::STR_BOOK, StrId::STR_CHAPTER, StrId::STR_STATE_OFF},
          [] { return SETTINGS.readerStatusItem(0); }, [](uint8_t v) { SETTINGS.setReaderStatusItem(0, v); },
          "readerStatusTop", StrId::STR_CUSTOMISE_STATUS_BAR));
    for (int row = 1; row <= 3; ++row) {
      const StrId names[] = {StrId::STR_READER_STATUS_LEFT, StrId::STR_READER_STATUS_CENTER,
                             StrId::STR_READER_STATUS_RIGHT};
      const char* keys[] = {"readerStatusLeft", "readerStatusCenter", "readerStatusRight"};
      emit(SettingInfo::DynamicEnum(names[row - 1],
            {StrId::STR_STATE_OFF, StrId::STR_CLOCK, StrId::STR_BATTERY, StrId::STR_CHAPTER_PAGE_COUNT,
             StrId::STR_BOOK_PROGRESS_PERCENTAGE, StrId::STR_READER_STATUS_CHAPTER_ETA, StrId::STR_READER_STATUS_BOOK_ETA},
            [row] { return SETTINGS.readerStatusItem(row); },
            [row](uint8_t v) { SETTINGS.setReaderStatusItem(row, v); }, keys[row - 1], StrId::STR_CUSTOMISE_STATUS_BAR));
    }
    emit(SettingInfo::Toggle(StrId::STR_CHAPTER_PAGE_COUNT, &CrossPointSettings::statusBarChapterPageCount,
                            "statusBarChapterPageCount", StrId::STR_CUSTOMISE_STATUS_BAR));
    emit(SettingInfo::Toggle(StrId::STR_BOOK_PROGRESS_PERCENTAGE, &CrossPointSettings::statusBarBookProgressPercentage,
                            "statusBarBookProgressPercentage", StrId::STR_CUSTOMISE_STATUS_BAR));
    emit(SettingInfo::Enum(StrId::STR_PROGRESS_BAR, &CrossPointSettings::statusBarProgressBar,
                          {StrId::STR_BOOK, StrId::STR_CHAPTER, StrId::STR_HIDE}, "statusBarProgressBar",
                          StrId::STR_CUSTOMISE_STATUS_BAR));
    emit(SettingInfo::Enum(StrId::STR_PROGRESS_BAR_THICKNESS, &CrossPointSettings::statusBarProgressBarThickness,
                          {StrId::STR_PROGRESS_BAR_THIN, StrId::STR_PROGRESS_BAR_MEDIUM, StrId::STR_PROGRESS_BAR_THICK},
                          "statusBarProgressBarThickness", StrId::STR_CUSTOMISE_STATUS_BAR));
    emit(SettingInfo::Enum(StrId::STR_TITLE, &CrossPointSettings::statusBarTitle,
                          {StrId::STR_BOOK, StrId::STR_CHAPTER, StrId::STR_HIDE}, "statusBarTitle",
                          StrId::STR_CUSTOMISE_STATUS_BAR));
    emit(SettingInfo::Toggle(StrId::STR_BATTERY, &CrossPointSettings::statusBarBattery, "statusBarBattery",
                            StrId::STR_CUSTOMISE_STATUS_BAR));
    emit(SettingInfo::Enum(StrId::STR_XTC_STATUS_BAR, &CrossPointSettings::xtcStatusBarMode,
                          {StrId::STR_HIDE, StrId::STR_BOTTOM, StrId::STR_TOP}, "xtcStatusBarMode",
                          StrId::STR_CUSTOMISE_STATUS_BAR));
    // Clock entries (persistence + web settings; the device UI is
    // ClockSettingsActivity under System settings).
    emit(SettingInfo::Enum(StrId::STR_CLOCK, &CrossPointSettings::statusBarClock, std::move(statusBarClockValues),
                          "statusBarClock", StrId::STR_CUSTOMISE_STATUS_BAR));
    emit(SettingInfo::Toggle(StrId::STR_CLOCK_AUTO_TIMEZONE, &CrossPointSettings::clockAutoTimezone, "clockAutoTimezone",
                            StrId::STR_CUSTOMISE_STATUS_BAR));
    // LEGACY: retired quarter-hour UTC offset (biased by 48), still written so
    // timezones::activeIndex() can migrate it into clockTimezone (#3562).
    emit(SettingInfo::Value(StrId::STR_CLOCK_UTC_OFFSET, &CrossPointSettings::clockUtcOffsetQ, {0, 104, 1},
                           "clockUtcOffsetQ", StrId::STR_CUSTOMISE_STATUS_BAR));
    // Index into the append-only table in src/util/Timezones.cpp; 255 = unset.
    emit(SettingInfo::Value(StrId::STR_TIMEZONE, &CrossPointSettings::clockTimezone, {0, 255, 1}, "clockTimezone",
                           StrId::STR_CUSTOMISE_STATUS_BAR));
    emit(SettingInfo::Enum(StrId::STR_CLOCK_DST, &CrossPointSettings::clockDst,
                          {StrId::STR_CLOCK_DST_AUTO, StrId::STR_STATE_ON, StrId::STR_STATE_OFF}, "clockDst",
                          StrId::STR_CUSTOMISE_STATUS_BAR));
    emit(SettingInfo::Enum(StrId::STR_CLOCK_IN_HEADER, &CrossPointSettings::clockShowInHeader,
                          {StrId::STR_HIDE, StrId::STR_CLOCK_HEADER_TIME, StrId::STR_CLOCK_HEADER_TIME_DATE}, "clockShowHeader",
                          StrId::STR_CUSTOMISE_STATUS_BAR));
    emit(SettingInfo::Enum(StrId::STR_CLOCK_FORMAT, &CrossPointSettings::clockFormat,
                          {StrId::STR_CLOCK_FORMAT_24H, StrId::STR_CLOCK_FORMAT_12H}, "clockFormat",
                          StrId::STR_CUSTOMISE_STATUS_BAR));
    // Persistence flag for NTP debounce. Resetting from the web UI forces a re-sync
    // on next WiFi connect, which is useful when crossing time zones.
    emit(SettingInfo::Toggle(StrId::STR_CLOCK_SYNCED, &CrossPointSettings::clockHasBeenSynced, "clockHasBeenSynced",
                            StrId::STR_CUSTOMISE_STATUS_BAR));
    emit(SettingInfo::Enum(StrId::STR_UGLY_START_SCREEN, &CrossPointSettings::uglyStartScreen,
                          {StrId::STR_UGLY_START_BOOK, StrId::STR_UGLY_START_DIARY,
                           StrId::STR_UGLY_START_RECENT, StrId::STR_UGLY_START_DESK},
                          "uglyStartScreen", StrId::STR_CAT_DISPLAY));
}

inline const std::vector<SettingInfo>& getBaseSettingsList() {
  auto& baseList = settings_catalog::storage();
  if (!baseList.empty()) return baseList;
#if defined(TENOR_UI_ACCEPTANCE) && defined(ESP_PLATFORM)
  static bool catalogMeasured = false;
  if (!catalogMeasured) {
    logSerial.printf("SETTING_CATALOG:fixed:before,info_size=%u,size=0,capacity=0,free=%u,min=%u,largest=%u,loop_stack_free=%u\n",
                     static_cast<unsigned>(sizeof(SettingInfo)), ESP.getFreeHeap(), ESP.getMinFreeHeap(),
                     ESP.getMaxAllocHeap(), static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
  }
#endif
  // Counted, not written down: a hand count fell 2 rows short in v1.0.53 and the vector doubled to
  // 196 rows, 25,600 B on the X3 instead of 12,800.
  size_t rows = 0;
  forEachBaseSetting([&rows](SettingInfo&&) { ++rows; });
  baseList.reserve(rows);
  forEachBaseSetting([&baseList](SettingInfo&& info) { baseList.push_back(std::move(info)); });

#if defined(TENOR_UI_ACCEPTANCE) && defined(ESP_PLATFORM)
  if (!catalogMeasured) {
    logSerial.printf("SETTING_CATALOG:fixed:after,info_size=%u,size=%u,capacity=%u,free=%u,min=%u,largest=%u,loop_stack_free=%u\n",
                     static_cast<unsigned>(sizeof(SettingInfo)), static_cast<unsigned>(baseList.size()),
                     static_cast<unsigned>(baseList.capacity()), ESP.getFreeHeap(), ESP.getMinFreeHeap(),
                     ESP.getMaxAllocHeap(), static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    catalogMeasured = true;
  }
#endif
  return baseList;
}

// Dong nao khong thuoc ve ban may nay. Tach rieng de duong luu va duong doc soi
// duoc tung dong ma khong phai chep ca bang ra mot vector moi.
inline bool settingHiddenOnThisBoard(const SettingInfo& s) {
  // Touch controls and the per-direction gestures they drive need a touch
  // controller. The toolbar reader menu style does not (#3603 re-enabled it
  // on button-only boards: the toolbar chrome is button-navigable).
  if (!BoardConfig::hasTouch() &&
      (s.nameId == StrId::STR_TOUCH_READER_CONTROLS || s.nameId == StrId::STR_NEXT_PAGE_GESTURE ||
       s.nameId == StrId::STR_PREV_PAGE_GESTURE || s.nameId == StrId::STR_BACK_TAP_ZONE))
    return true;
  // X4 Pro only (#3089); the frontlight double-click shortcut needs its I2C frontlight. The tap-zone tip is
  // the touch shell's.
  if (!BoardConfig::isX4Pro() && (s.nameId == StrId::STR_DBL_CLICK_PWR_LIGHT || s.nameId == StrId::STR_READER_TAP_TIP))
    return true;
  // Home button shortcuts (#3516) need a physical Home key.
  if (!BoardConfig::hasHomeKey() && home_button::isSetting(s.valuePtr)) return true;
  // Khong co den nen thi hai dong do ngoi khong. X3 va X4 khai NO_FRONTLIGHT,
  // X4 Pro co den nen giu lai. Phai hoi CA HAI kieu day den: mot bang day den qua
  // I2C chu khong phai PWM, chi hoi PWM la giau mat dong cua may that su co den.
  if (!BoardConfig::hasPwmFrontlight() && !BoardConfig::hasI2cFrontlight() &&
      (s.nameId == StrId::STR_RESTORE_LIGHT_ON_WAKE || s.nameId == StrId::STR_BRIGHTNESS))
    return true;
  // Cu chi mo menu doc chi co nghia o may con phim Home cam ung, vi cho khac thi
  // vuot canh duoi la ve Home va cham giua moi la duong chinh.
  if (!BoardConfig::hasHomeKey() && s.nameId == StrId::STR_SHOW_READER_MENU) return true;
  // Only X3 sleeps with its panel unpowered; the other boards keep their sleep refresh and wake with
  // no kept frame to draw the notice over.
  if ((s.nameId == StrId::STR_SLEEP_BW_REFRESH || s.nameId == StrId::STR_WAKE_NOTICE) &&
      BoardConfig::ACTIVE.board != BoardConfig::Board::XteinkX3 &&
      BoardConfig::ACTIVE.board != BoardConfig::Board::XteinkX3Uc8279)
    return true;
  if (BoardConfig::hasTouch() &&
      (s.nameId == StrId::STR_FRONT_BTN_FOLLOW_ORIENTATION || s.nameId == StrId::STR_SUNLIGHT_FADING_FIX ||
       s.nameId == StrId::STR_BACK_SHORT_TO_FILE_BROWSER))
    return true;
  return false;
}

// The device category list owns only rows visible in its categories. Font and
// spacing descriptors stay in Text Settings; their dynamic options are built
// there when needed. Return -1 for rows hidden in device categories.
inline bool settingHiddenInShell(const SettingInfo& setting) {
  const bool ugly = SETTINGS.uiShell == 1;
  return (ugly && setting.valuePtr == &CrossPointSettings::wakeIntoBook) ||
         (!ugly && setting.valuePtr == &CrossPointSettings::uglyStartScreen);
}

inline int deviceSettingsTab(const SettingInfo& setting) {
  if (settingHiddenOnThisBoard(setting) || settingHiddenInShell(setting)) return -1;
  // X4 Pro: three rows made for front buttons do nothing here. They leave the lists, not the saved file.
  if (BoardConfig::isX4Pro() && (setting.nameId == StrId::STR_SIDE_ARROW_HINTS || setting.nameId == StrId::STR_BUTTON_LABELS ||
                                 setting.nameId == StrId::STR_READER_MENU_STYLE))
    return -1;
  if (setting.valuePtr == &CrossPointSettings::sleepScreen ||
      setting.valuePtr == &CrossPointSettings::sleepScreenCoverMode ||
      setting.valuePtr == &CrossPointSettings::sleepScreenCoverFilter ||
      setting.valuePtr == &CrossPointSettings::quickResumeSleepScreen ||
      setting.valuePtr == &CrossPointSettings::wakeIntoBook ||
      setting.valuePtr == &CrossPointSettings::uglyStartScreen ||
      setting.valuePtr == &CrossPointSettings::wakeNotice ||
      setting.valuePtr == &CrossPointSettings::sleepBwFold ||
      setting.valuePtr == &CrossPointSettings::sleepTimeoutMinutes ||
      setting.valuePtr == &CrossPointSettings::frontlightRestoreOnWake)
    return static_cast<int>(settingstabs::Tab::SLEEP);
  if (setting.valuePtr == &CrossPointSettings::statusBarClock)
    return static_cast<int>(settingstabs::Tab::SCREEN);
  if (setting.category == StrId::STR_CAT_DISPLAY) {
    if (setting.valuePtr == &CrossPointSettings::fadingFix &&
        (BoardConfig::isX4Pro() || BoardConfig::isX4Classic())) return -1;
    return static_cast<int>(settingstabs::Tab::SCREEN);
  }
  if (setting.category == StrId::STR_CAT_READER)
    return setting.inTextSettings ? -1 : static_cast<int>(settingstabs::Tab::READER);
  if (setting.category == StrId::STR_CAT_CONTROLS) {
    if (setting.valuePtr == &CrossPointSettings::pwrBtnFootnoteBack &&
        SETTINGS.shortPwrBtn != CrossPointSettings::SHORT_PWRBTN::FOOTNOTES) return -1;
    return static_cast<int>(settingstabs::Tab::CONTROLS);
  }
  if (setting.category == StrId::STR_CAT_KEYBOARD) return static_cast<int>(settingstabs::Tab::KEYBOARD);
  if (setting.category == StrId::STR_CAT_MOTION) return static_cast<int>(settingstabs::Tab::MOTION);
  if (setting.category == StrId::STR_CAT_SYSTEM) {
    return static_cast<int>(settingstabs::Tab::SYSTEM);
  }
  return -1;
}

// Settings tabs this board shows: all of them with a motion sensor, else every one but
// Gestures, the last by ID.
static_assert(static_cast<int>(settingstabs::Tab::MOTION) == settingstabs::TAB_COUNT - 1,
              "a board without a motion sensor drops the last tab");
inline int deviceSettingsTabCount() {
  return halTiltSensor.isAvailable() ? settingstabs::TAB_COUNT : settingstabs::TAB_COUNT - 1;
}

inline std::vector<SettingInfo> getSettingsList(const SdCardFontRegistry* registry = nullptr,
                                              const std::vector<DictionaryEntry>* dictionaries = nullptr) {
  std::vector<SettingInfo> v = getBaseSettingsList();
  v.erase(std::remove_if(v.begin(), v.end(), settingHiddenOnThisBoard), v.end());
  v.erase(std::remove_if(v.begin(), v.end(), settingHiddenInShell), v.end());
  // The web page saves values without shell::changed(), so the shell is chosen on the device only.
  v.erase(std::remove_if(v.begin(), v.end(),
                         [](const SettingInfo& s) {
                           return s.valuePtr == &CrossPointSettings::uiShell || s.valuePtr == &CrossPointSettings::uiUglyLevel ||
                                  s.valuePtr == &CrossPointSettings::uiShellSleepMemo ||
                                  s.valuePtr == &CrossPointSettings::uiShellClockMemo ||
                                  s.valuePtr == &CrossPointSettings::uiShellClockOnce ||
                                  s.valuePtr == &CrossPointSettings::uglyBatteryHidden;
                         }),
          v.end());
  if (registry && registry->getFamilyCount() > 0) {
    auto it = std::find_if(v.begin(), v.end(), [](const SettingInfo& s) { return s.nameId == StrId::STR_FONT_FAMILY; });
    if (it != v.end()) {
      *it = buildFontFamilySetting(registry);
    }
  }
  {
    // Unconditional: even with no SD fonts installed the sizes come from the
    // built-in family rather than a fixed Small/Medium/Large/XL enum.
    auto it = std::find_if(v.begin(), v.end(), [](const SettingInfo& s) { return s.nameId == StrId::STR_FONT_SIZE; });
    if (it != v.end()) {
      *it = buildFontSizeSetting(registry);
    }
  }
  if (dictionaries && !dictionaries->empty()) {
    // Insert at the end of the Reader category (just before the first Controls entry).
    auto it =
        std::find_if(v.begin(), v.end(), [](const SettingInfo& s) { return s.category == StrId::STR_CAT_CONTROLS; });
    v.insert(it, buildDictionarySetting(*dictionaries));
  }
  return v;
}
