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
#include <span>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "HomeButtonSettings.h"
#include "KOReaderCredentialStore.h"
#include "QuickAction.h"
#include "activities/home/FileBrowserState.h"
#include "ReaderFontSizes.h"
#include "shells/UglyFont.h"
#include "shells/ugly/UglyLevel.h"
#include "activities/settings/SettingsActivity.h"
#include "components/UITheme.h"
#include "platform/SimulatorBoardCompat.h"
#include "util/DictionaryRegistry.h"

// Build the font family setting dynamically. When registry is non-null, SD card fonts
// are appended after the built-in fonts. Otherwise only built-in fonts are listed.
inline SettingInfo buildFontFamilySetting(const SdCardFontRegistry* registry) {
  // Built-in font labels (StrId)
  std::vector<StrId> enumValues = {StrId::STR_NOTO_SANS};
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
    uglyfont::pickedByHand(SETTINGS.uiShellFontMemo);
    SETTINGS.fontFallbackNotice = false;  // a font picked by hand: the notice about the old one is moot
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
// Page-turn hold: Off, chapter, orientation, side buttons back. The list skips number 3 (retired), so the row is
// dynamic and saved by hand (CrossPointSettings.cpp); list position 3 is number 4.
inline SettingInfo buildLongPressButtonSetting() {
  return SettingInfo::DynamicEnum(
      StrId::STR_LONG_PRESS_BEHAVIOR,
      {StrId::STR_LONG_PRESS_BEHAVIOR_OFF, StrId::STR_LONG_PRESS_BEHAVIOR_SKIP,
       StrId::STR_LONG_PRESS_BEHAVIOR_ORIENTATION, StrId::STR_LONG_PRESS_BEHAVIOR_SIDE_BACK},
      []() -> uint8_t {
        const uint8_t number = SETTINGS.longPressButtonBehavior;
        return number == CrossPointSettings::SIDE_HOLD_BACK ? 3 : number;
      },
      [](const uint8_t position) {
        SETTINGS.longPressButtonBehavior = position == 3 ? CrossPointSettings::SIDE_HOLD_BACK : position;
      },
      "longPressButtonBehavior", StrId::STR_CAT_CONTROLS);
}

inline SettingInfo buildLongPressMenuSetting(const bool hasTilt) {
  static constexpr StrId LABELS[] = {StrId::STR_KOSYNC,     StrId::STR_DISABLED,        StrId::STR_BOOKMARK_OPTION,
                                     StrId::STR_DICTIONARY, StrId::STR_READER_MENU,     StrId::STR_FILE_TRANSFER,
                                     StrId::STR_TILT_PAGE_TURN, StrId::STR_QUOTES_SAVE_ACTION,
                                     StrId::STR_BLE_CONNECT_REMOTE, StrId::STR_SAVE_CLIPPING, StrId::STR_FOOTNOTES};
  static_assert(std::size(LABELS) == CrossPointSettings::LONG_PRESS_MENU_FUNCTION_COUNT, "one label per function");
  // The list shows the functions this board offers; the table keeps the stored number behind each place, so a
  // function left out (tilt without an IMU, the remote without Bluetooth) never shifts the ones after it.
  struct Offered {
    uint8_t number[std::size(LABELS)];
    uint8_t count = 0;
  } offeredNumbers;
  std::vector<StrId> offered;
  offered.reserve(std::size(LABELS));
  for (uint8_t number = 0; number < std::size(LABELS); ++number) {
#if !FREEINK_CAP_BLE_HID_HOST
    if (number == CrossPointSettings::LP_MENU_CONNECT_REMOTE) continue;
#endif
    if (!hasTilt && number == CrossPointSettings::LP_MENU_TILT_PAGE_TURN) continue;
    offeredNumbers.number[offeredNumbers.count++] = number;
    offered.push_back(LABELS[number]);
  }
  return SettingInfo::DynamicEnum(
      StrId::STR_LONG_PRESS_MENU, std::move(offered),
      [offeredNumbers]() -> uint8_t {
        for (uint8_t place = 0; place < offeredNumbers.count; ++place) {
          if (offeredNumbers.number[place] == SETTINGS.longPressMenuFunction) return place;
        }
        return CrossPointSettings::LP_MENU_DISABLED;  // not offered here: the list shows Disabled
      },
      [offeredNumbers](const uint8_t position) {
        SETTINGS.longPressMenuFunction = offeredNumbers.number[std::min<uint8_t>(position, offeredNumbers.count - 1)];
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

// The "On wake" row: three choices on tenor/cross, four on tenor/ugly (which has a root screen of its
// own). The menu order is not the stored order. Home and Recent are one place on cross: its last choice
// stores Home, and a Recent stored on ugly reads as Home there.
inline SettingInfo buildWakeTargetSetting(const SettingInfo& registered) {
  using S = CrossPointSettings;
  static constexpr uint8_t CROSS[] = {S::WAKE_RESUME, S::WAKE_BOOK, S::WAKE_HOME};
  static constexpr uint8_t UGLY[] = {S::WAKE_RESUME, S::WAKE_BOOK, S::WAKE_HOME, S::WAKE_RECENT};
  const bool ugly = SETTINGS.uiShell == 1;
  const std::span<const uint8_t> targets = ugly ? std::span<const uint8_t>(UGLY) : std::span<const uint8_t>(CROSS);
  SettingInfo setting = registered;
  setting.valuePtr = nullptr;
  setting.enumValues = {StrId::STR_WAKE_RESUME, StrId::STR_UGLY_START_BOOK};
  if (ugly) setting.enumValues.push_back(StrId::STR_UGLY_ROOT_SCREEN);
  setting.enumValues.push_back(ugly ? StrId::STR_UGLY_START_RECENT : StrId::STR_WAKE_HOME);
  setting.valueGetter = [targets]() -> uint8_t {
    const auto at = std::find(targets.begin(), targets.end(), SETTINGS.wakeTarget);
    return static_cast<uint8_t>(at == targets.end() ? targets.size() - 1 : at - targets.begin());
  };
  setting.valueSetter = [targets](const uint8_t position) {
    SETTINGS.wakeTarget = targets[std::min<size_t>(position, targets.size() - 1)];
  };
  return setting;
}

inline SettingInfo buildGlobalStatusBarChoiceSetting() {
  return SettingInfo::DynamicEnum(
      StrId::STR_GLOBAL_STATUS_BAR_CHOICE,
      {StrId::STR_STATUS_BAR_HIDE_ALL, StrId::STR_STATUS_BAR_TOP_SMALL, StrId::STR_STATUS_BAR_TOP_LARGE,
       StrId::STR_STATUS_BAR_BOTTOM_SMALL, StrId::STR_STATUS_BAR_BOTTOM_LARGE},
      [] { return SETTINGS.globalStatusBarChoice(); },
      [](const uint8_t value) { SETTINGS.setGlobalStatusBarChoice(value); }, "globalStatusBarChoice",
      StrId::STR_CAT_DISPLAY);
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
  static constexpr StrId ink[] = {StrId::STR_READER_INK_M3, StrId::STR_READER_INK_M2, StrId::STR_READER_INK_M1,
                                  StrId::STR_READER_INK_0,  StrId::STR_READER_INK_1,  StrId::STR_READER_INK_2,
                                  StrId::STR_READER_INK_3,  StrId::STR_READER_INK_4,  StrId::STR_READER_INK_5};
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

using SettingVisitor = void (*)(void* context, SettingInfo& info);

// Every base descriptor, built one at a time and handed to the visitor as an lvalue it may move
// from (SettingsList.cpp). Visits synchronously in list order; borrowed references are valid only
// during the callback. Settings save and load walk it here instead of keeping the whole catalog
// (~15 KB on the X3) for the life of the program. Its conditions must be fixed before settings
// load, or load and save would see different keys: runtime probes go to settingHiddenOnThisBoard().
void forEachBaseSetting(SettingVisitor visit, void* context);

template <typename Fn>
void forEachBaseSetting(Fn& fn) {
  forEachBaseSetting([](void* context, SettingInfo& info) { (*static_cast<Fn*>(context))(info); }, &fn);
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
  forEachBaseSetting([](void* context, SettingInfo&) { ++*static_cast<size_t*>(context); }, &rows);
  baseList.reserve(rows);
  forEachBaseSetting(
      [](void* context, SettingInfo& info) {
        static_cast<std::vector<SettingInfo>*>(context)->push_back(std::move(info));
      },
      &baseList);

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
  // The 3 Home styles are drawn on the touch shell and on the X3 (tenor/ink 1 bit); the X4 keeps its card.
  if (s.nameId == StrId::STR_HOME_STYLE && !BoardConfig::hasTouch() &&
      BoardConfig::ACTIVE.board != BoardConfig::Board::XteinkX3 &&
      BoardConfig::ACTIVE.board != BoardConfig::Board::XteinkX3Uc8279)
    return true;
  if (BoardConfig::hasTouch() && s.nameId == StrId::STR_TIPS) return true;
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
  // Nothing reads this one: a short Back leaves the book for Recent/Continue (ReaderUtils::handleBackNavigation).
  // It leaves every list on every board but stays in the saved file, so an old settings.json keeps its value.
  return setting.nameId == StrId::STR_BACK_SHORT_TO_FILE_BROWSER ||
         (ugly && setting.valuePtr == &CrossPointSettings::tipsEnabled) ||
         (!ugly && setting.valuePtr == &CrossPointSettings::uglyRootScreen);
}

// Motion rows stay in the saved file on a board with a sensor, and leave every list while this boot's
// probe found none (CrossPoint #3385).
inline bool motionRowWithoutSensor(const SettingInfo& setting) {
  return setting.category == StrId::STR_CAT_MOTION && !halTiltSensor.isAvailable();
}

inline int deviceSettingsTab(const SettingInfo& setting) {
  if (settingHiddenOnThisBoard(setting) || settingHiddenInShell(setting) || motionRowWithoutSensor(setting)) return -1;
  // X4 Pro: four rows made for front buttons do nothing here. They leave the lists, not the saved file.
  if (BoardConfig::isX4Pro() && (setting.nameId == StrId::STR_SIDE_ARROW_HINTS || setting.nameId == StrId::STR_BUTTON_LABELS ||
                                 setting.nameId == StrId::STR_BUTTON_HINTS ||
                                 setting.nameId == StrId::STR_READER_MENU_STYLE))
    return -1;
  if (setting.valuePtr == &CrossPointSettings::sleepScreen ||
      setting.valuePtr == &CrossPointSettings::sleepScreenCoverMode ||
      setting.valuePtr == &CrossPointSettings::sleepScreenCoverFilter ||
      setting.valuePtr == &CrossPointSettings::quickResumeSleepScreen ||
      setting.valuePtr == &CrossPointSettings::wakeTarget ||
      setting.valuePtr == &CrossPointSettings::wakeNotice ||
      setting.valuePtr == &CrossPointSettings::sleepBwFold ||
      setting.valuePtr == &CrossPointSettings::sleepTimeoutMinutes ||
      setting.valuePtr == &CrossPointSettings::frontlightRestoreOnWake ||
      setting.valuePtr == &CrossPointSettings::frontlightFromHour ||
      setting.valuePtr == &CrossPointSettings::frontlightUntilHour)
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
                           if (motionRowWithoutSensor(s)) return true;
                           return s.valuePtr == &CrossPointSettings::uiShell || s.valuePtr == &CrossPointSettings::uiUglyLevel ||
                                  s.valuePtr == &CrossPointSettings::uiShellSleepMemo ||
                                  s.valuePtr == &CrossPointSettings::uiShellClockMemo ||
                                  s.valuePtr == &CrossPointSettings::uiShellClockOnce ||
                                  s.valuePtr == &CrossPointSettings::uiShellFontMemo ||
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
