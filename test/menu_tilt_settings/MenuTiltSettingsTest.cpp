#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "QuickAction.h"
#include "SettingsList.h"

namespace {

bool expect(bool condition, const char* label) {
  if (!condition) std::printf("FAIL %s\n", label);
  return condition;
}

const SettingInfo* findSetting(const std::vector<SettingInfo>& catalog, const char* key) {
  const auto it = std::find_if(catalog.begin(), catalog.end(), [key](const SettingInfo& setting) {
    return setting.key && std::strcmp(setting.key, key) == 0;
  });
  return it == catalog.end() ? nullptr : &*it;
}

bool isTiltEnum(const SettingInfo& setting, const StrId category) {
  return setting.type == SettingType::ENUM && setting.category == category &&
         setting.enumValues == std::vector<StrId>{StrId::STR_STATE_OFF, StrId::STR_NORMAL, StrId::STR_TILT_INVERTED};
}

bool isStrengthEnum(const SettingInfo& setting, const StrId name, uint8_t CrossPointSettings::* field) {
  return setting.type == SettingType::ENUM && setting.category == StrId::STR_CAT_MOTION && setting.nameId == name &&
         setting.valuePtr == field &&
         setting.enumValues ==
             std::vector<StrId>{StrId::STR_TILT_LIGHT, StrId::STR_UI_SIZE_MEDIUM, StrId::STR_TILT_STRONG};
}

// Confirm-hold functions, in stored-index order. Tilt page turn is last so a
// board without an IMU can drop it without shifting any saved index.
bool longPressValuesMatch(const std::vector<SettingInfo>& catalog, const bool hasImu) {
  const SettingInfo* longPress = findSetting(catalog, "longPressMenuFunction");
  if (!longPress) return false;
  std::vector<StrId> expected{StrId::STR_KOSYNC,     StrId::STR_DISABLED,    StrId::STR_BOOKMARK_OPTION,
                              StrId::STR_DICTIONARY, StrId::STR_READER_MENU, StrId::STR_FILE_TRANSFER};
  if (hasImu) expected.push_back(StrId::STR_TILT_PAGE_TURN);
  expected.push_back(StrId::STR_QUOTES_SAVE_ACTION);
  return longPress->enumValues == expected &&
         expected.size() == (hasImu ? CrossPointSettings::LONG_PRESS_MENU_FUNCTION_COUNT
                                    : CrossPointSettings::LONG_PRESS_MENU_FUNCTION_COUNT - 1U);
}

// Label of each stored Confirm-hold number (0-7), whatever list position a board shows it at.
constexpr StrId LONG_PRESS_LABELS[] = {StrId::STR_KOSYNC,     StrId::STR_DISABLED,    StrId::STR_BOOKMARK_OPTION,
                                       StrId::STR_DICTIONARY, StrId::STR_READER_MENU, StrId::STR_FILE_TRANSFER,
                                       StrId::STR_TILT_PAGE_TURN, StrId::STR_QUOTES_SAVE_ACTION};

// Every stored Confirm-hold number reads back as itself and saves as itself; the list shows its label at the
// position the board offers it. Without an IMU the tilt number (6) is not offered and falls back to Off, while
// Save quotation (7) takes list position 6 and is still stored as 7.
bool longPressStoreKept(const std::vector<SettingInfo>& catalog, const bool hasImu) {
  const SettingInfo* longPress = findSetting(catalog, "longPressMenuFunction");
  if (!expect(longPress && longPress->valueGetter && longPress->valueSetter, "Confirm-hold row maps list position to number")) {
    return false;
  }
  bool ok = true;
  for (int stored = 0; stored < 10; ++stored) {
    JsonDocument in;
    in["tenorPresetVersion"] = CrossPointSettings::TENOR_PRESET_VERSION;
    in["longPressMenuFunction"] = stored;
    SETTINGS.longPressMenuFunction = CrossPointSettings::LP_MENU_DISABLED;
    ok = expect(SETTINGS.fromJson(in.as<JsonVariantConst>()), "Confirm-hold JSON loads") && ok;
    const bool offered = stored < 8 && (hasImu || stored != 6);
    const uint8_t want = offered ? static_cast<uint8_t>(stored) : CrossPointSettings::LP_MENU_DISABLED;
    ok = expect(SETTINGS.longPressMenuFunction == want, "stored Confirm-hold number is kept, an unoffered one falls to Off") && ok;
    JsonDocument out;
    SETTINGS.toJson(out);
    ok = expect(out["longPressMenuFunction"].as<int>() == want, "Confirm-hold number saves as the number it holds") && ok;
    const uint8_t position = longPress->valueGetter();
    ok = expect(position < longPress->enumValues.size() && longPress->enumValues[position] == LONG_PRESS_LABELS[want],
                "list position shows the label of the stored function") &&
         ok;
    longPress->valueSetter(position);
    ok = expect(SETTINGS.longPressMenuFunction == want, "choosing the shown position stores the same number") && ok;
  }
  return ok;
}

// A short press wakes the device only where a short press also puts it to sleep.
int runPowerWake() {
  bool ok = true;
  for (uint8_t mode = 0; mode < CrossPointSettings::SHORT_PWRBTN_COUNT; ++mode) {
    const bool sleepMode = mode == CrossPointSettings::SLEEP;
    ok = expect(CrossPointSettings::acceptPowerWake(mode, true), "a verified hold always wakes") && ok;
    ok = expect(CrossPointSettings::acceptPowerWake(mode, false) == sleepMode,
                sleepMode ? "Sleep mode wakes on a tap, X3 included" : "other modes still need the hold") &&
         ok;
  }
  std::printf("menu_tilt_settings=power:%s\n", ok ? "GREEN" : "RED");
  return ok ? 0 : 1;
}

// A short power press, a hard shake, face down, face up and a double tap share one decision:
// what the action means on the screen in front, or nothing there. Back and Select are pressed like the keys on
// every screen; the screen takes or ignores them as it does the real ones.
int runQuickActions() {
  using quickaction::Outcome;
  using quickaction::Trigger;
  struct Screen {
    const char* name;
    bool reader;
  };
  constexpr Screen SCREENS[] = {
      {"home", false}, {"menu", false}, {"list", false}, {"settings", false}, {"reader", true}};
  bool ok = true;
  for (const auto& screen : SCREENS) {
    for (uint8_t action = 0; action < CrossPointSettings::SHORT_PWRBTN_COUNT; ++action) {
      Outcome shake = Outcome::None;
      Outcome power = Outcome::None;
      Outcome touchPower = Outcome::None;
      switch (action) {
        case CrossPointSettings::FORCE_REFRESH:
          shake = power = touchPower = Outcome::Refresh;
          break;
        case CrossPointSettings::SLEEP:
          shake = Outcome::Sleep;  // the button sleeps on its press
          break;
        case CrossPointSettings::PAGE_TURN:
          if (screen.reader) shake = Outcome::PageForward;  // the button's own turn is the reader's
          break;
        case CrossPointSettings::PWR_CONFIRM:
          shake = power = Outcome::Confirm;  // touch boards: the input map selects on the release
          break;
        case CrossPointSettings::BACK:
          shake = power = touchPower = Outcome::Back;
          break;
        case CrossPointSettings::READER_MENU:  // the reader's own shortcuts: nothing elsewhere
          if (screen.reader) shake = power = touchPower = Outcome::ReaderMenu;
          break;
        case CrossPointSettings::SAVE_QUOTE:
          if (screen.reader) shake = power = touchPower = Outcome::SaveQuote;
          break;
        default:
          break;
      }
      const bool reader = screen.reader;
      if (quickaction::resolve(action, Trigger::Shake, reader) != shake ||
          quickaction::resolve(action, Trigger::Shake, reader, true) != shake) {
        std::printf("FAIL shake action %u on %s\n", action, screen.name);
        ok = false;
      }
      // Face down, face up, the double taps and a remote button are triggers like the shake: the
      // same outcome on every screen. A double tap's Page turn outside a book presses the side
      // button the page turns with, so there it steps the tabs as the side buttons do.
      for (const Trigger flip :
           {Trigger::FaceDown, Trigger::FaceUp, Trigger::DoubleTap, Trigger::ScreenTap, Trigger::EdgeTap, Trigger::Remote}) {
        const bool tap = flip == Trigger::DoubleTap || flip == Trigger::ScreenTap || flip == Trigger::EdgeTap;
        const Outcome want = tap && action == CrossPointSettings::PAGE_TURN && !reader ? Outcome::SideForward : shake;
        if (quickaction::resolve(action, flip, reader) != want || quickaction::resolve(action, flip, reader, true) != want) {
          std::printf("FAIL %s action %u on %s\n",
                      flip == Trigger::FaceDown ? "face down" : flip == Trigger::FaceUp ? "face up"
                                                           : flip == Trigger::DoubleTap ? "double tap"
                                                           : flip == Trigger::ScreenTap ? "screen tap"
                                                           : flip == Trigger::EdgeTap ? "edge tap"
                                                                                        : "remote",
                      action, screen.name);
          ok = false;
        }
      }
      if (quickaction::resolve(action, Trigger::PowerRelease, reader) != power ||
          quickaction::resolve(action, Trigger::PowerRelease, reader, true) != touchPower) {
        std::printf("FAIL power release action %u on %s\n", action, screen.name);
        ok = false;
      }
    }
  }

  // Both settings list the one catalog: the button every action at its stored value,
  // the shake Off then Refresh, Sleep, Page turn, Back, Select.
  const std::vector<StrId> power = quickaction::powerLabels();
  ok = expect(power == std::vector<StrId>{StrId::STR_IGNORE, StrId::STR_SLEEP, StrId::STR_PAGE_TURN,
                                          StrId::STR_FORCE_REFRESH, StrId::STR_FOOTNOTES, StrId::STR_SELECT,
                                          StrId::STR_SHAKE_BACK, StrId::STR_READER_MENU,
                                          StrId::STR_QUOTES_SAVE_ACTION},
              "the power button lists every action, old ones at their old places") &&
       ok;
  ok = expect(quickaction::shakeLabels() ==
                  std::vector<StrId>{StrId::STR_STATE_OFF, StrId::STR_FORCE_REFRESH, StrId::STR_SLEEP,
                                     StrId::STR_PAGE_TURN, StrId::STR_SHAKE_BACK, StrId::STR_SELECT},
              "the shake lists Off, Refresh, Sleep, Page turn, Back, Select") &&
       ok;
  for (uint8_t place = 0; place < std::size(quickaction::SHAKE_ORDER); ++place) {
    const uint8_t action = quickaction::shakeAsPowerAction(place);
    ok = expect(place == 0 ? action == CrossPointSettings::IGNORE
                           : quickaction::CHOICES[action].label == quickaction::shakeLabels()[place],
                "each shake row runs the action its label names") &&
         ok;
  }
  ok = expect(quickaction::shakeAsPowerAction(std::size(quickaction::SHAKE_ORDER)) == CrossPointSettings::IGNORE,
              "an unknown shake value does nothing") &&
       ok;
  std::printf("menu_tilt_settings=action:%s\n", ok ? "GREEN" : "RED");
  return ok ? 0 : 1;
}

// A settings file from before the shared list: every short power value it can hold
// reads back as the same action.
int runPowerValuesKept() {
  CrossPointSettings& settings = SETTINGS;
  bool ok = true;
  for (uint8_t value = CrossPointSettings::IGNORE; value <= CrossPointSettings::PWR_CONFIRM; ++value) {
    JsonDocument old;
    old["tenorPresetVersion"] = CrossPointSettings::TENOR_PRESET_VERSION;
    old["shortPwrBtn"] = value;
    settings.shortPwrBtn = CrossPointSettings::IGNORE;
    ok = expect(settings.fromJson(old.as<JsonVariantConst>()), "old power setting loads") && ok;
    ok = expect(settings.shortPwrBtn == value && quickaction::CHOICES[value].action == value,
                "an old power value keeps its action") &&
         ok;
  }
  JsonDocument back;
  back["tenorPresetVersion"] = CrossPointSettings::TENOR_PRESET_VERSION;
  back["shortPwrBtn"] = CrossPointSettings::BACK;
  ok = expect(settings.fromJson(back.as<JsonVariantConst>()) && settings.shortPwrBtn == CrossPointSettings::BACK,
              "the new Back value round trips") &&
       ok;
  std::printf("menu_tilt_settings=power-values:%s\n", ok ? "GREEN" : "RED");
  return ok ? 0 : 1;
}

// The two hard shake rows: in Gestures after the flick strengths, Off and Medium on
// a new card and on a file from before them, saved and read back, out-of-range values kept out.
int runShakeSettings() {
  halTiltSensor.available = true;
  CrossPointSettings& settings = SETTINGS;
  bool ok = expect(settings.shakeAction == 0 &&
                       settings.shakeStrength == CrossPointSettings::TILT_STRENGTH_MEDIUM,
                   "a new card starts with shake Off, strength Medium");
  const auto& catalog = getBaseSettingsList();
  const SettingInfo* action = findSetting(catalog, "shakeAction");
  const SettingInfo* strength = findSetting(catalog, "shakeStrength");
  const SettingInfo* strengthV = findSetting(catalog, "tiltStrengthV");
  ok = expect(action && strength && strengthV, "both shake rows present with the sensor") && ok;
  if (!action || !strength || !strengthV) return 1;
  ok = expect(action == strengthV + 1 && strength == strengthV + 2, "shake rows follow the flick strengths") && ok;
  ok = expect(action->type == SettingType::ENUM && action->category == StrId::STR_CAT_MOTION &&
                  action->nameId == StrId::STR_SHAKE_ACTION && action->valuePtr == &CrossPointSettings::shakeAction &&
                  action->enumValues == quickaction::shakeLabels(),
              "action row lists the shake's choices from the shared catalog") &&
       ok;
  ok = expect(isStrengthEnum(*strength, StrId::STR_SHAKE_STRENGTH, &CrossPointSettings::shakeStrength),
              "strength row reuses Light, Medium, Strong") &&
       ok;

  JsonDocument before;
  before["tenorPresetVersion"] = CrossPointSettings::TENOR_PRESET_VERSION;
  before["tiltPageTurn"] = CrossPointSettings::TILT_NORMAL;
  ok = expect(settings.fromJson(before.as<JsonVariantConst>()), "file from before the shake rows loads") && ok;
  ok = expect(settings.shakeAction == 0 && settings.shakeStrength == CrossPointSettings::TILT_STRENGTH_MEDIUM,
              "a file without the keys keeps shake Off, strength Medium") &&
       ok;
  {
    JsonDocument saved;
    settings.toJson(saved);
    ok = expect((saved["shakeAction"] | uint8_t{255}) == 0 &&
                    (saved["shakeStrength"] | uint8_t{255}) == CrossPointSettings::TILT_STRENGTH_MEDIUM,
                "the next save writes both keys") &&
         ok;
  }
  const uint8_t shakeCount = static_cast<uint8_t>(std::size(quickaction::SHAKE_ORDER));
  for (uint8_t a = 0; a < shakeCount; ++a) {
    for (uint8_t st = 0; st < CrossPointSettings::TILT_STRENGTH_COUNT; ++st) {
      settings.shakeAction = a;
      settings.shakeStrength = st;
      JsonDocument saved;
      settings.toJson(saved);
      settings.shakeAction = 0;
      settings.shakeStrength = CrossPointSettings::TILT_STRENGTH_MEDIUM;
      ok = expect(settings.fromJson(saved.as<JsonVariantConst>()), "shake JSON loads") && ok;
      ok = expect(settings.shakeAction == a && settings.shakeStrength == st, "every action and strength round trips") &&
           ok;
    }
  }
  settings.shakeAction = 0;
  settings.shakeStrength = CrossPointSettings::TILT_STRENGTH_MEDIUM;
  JsonDocument corrupt;
  corrupt["tenorPresetVersion"] = CrossPointSettings::TENOR_PRESET_VERSION;
  corrupt["shakeAction"] = shakeCount;
  corrupt["shakeStrength"] = 200;
  ok = expect(settings.fromJson(corrupt.as<JsonVariantConst>()), "corrupt shake JSON loads") && ok;
  ok = expect(settings.shakeAction == 0 && settings.shakeStrength == CrossPointSettings::TILT_STRENGTH_MEDIUM,
              "out-of-range values keep Off and Medium") &&
       ok;
  std::printf("menu_tilt_settings=shake:%s\n", ok ? "GREEN" : "RED");
  return ok ? 0 : 1;
}

// Face down and face up: after the shake rows in Gestures, the shake's own list, Off on
// a new card and on a file from before them, every value saved and read back, out-of-range Off.
int runFlipSettings() {
  halTiltSensor.available = true;
  CrossPointSettings& settings = SETTINGS;
  bool ok = expect(settings.faceDownAction == 0 && settings.faceUpAction == 0, "a new card starts with both Off");
  const auto& catalog = getBaseSettingsList();
  const SettingInfo* shakeStrength = findSetting(catalog, "shakeStrength");
  const SettingInfo* down = findSetting(catalog, "faceDownAction");
  const SettingInfo* up = findSetting(catalog, "faceUpAction");
  ok = expect(shakeStrength && down && up, "both rows present with the sensor") && ok;
  if (!shakeStrength || !down || !up) return 1;
  ok = expect(down == shakeStrength + 1 && up == shakeStrength + 2, "face down, then face up, after the shake") && ok;
  ok = expect(down->type == SettingType::ENUM && down->category == StrId::STR_CAT_MOTION &&
                  down->nameId == StrId::STR_FACE_DOWN_ACTION && down->valuePtr == &CrossPointSettings::faceDownAction &&
                  down->enumValues == quickaction::shakeLabels(),
              "face down lists the shake's choices") &&
       ok;
  ok = expect(up->type == SettingType::ENUM && up->category == StrId::STR_CAT_MOTION &&
                  up->nameId == StrId::STR_FACE_UP_ACTION && up->valuePtr == &CrossPointSettings::faceUpAction &&
                  up->enumValues == quickaction::shakeLabels(),
              "face up lists the shake's choices") &&
       ok;

  JsonDocument before;
  before["tenorPresetVersion"] = CrossPointSettings::TENOR_PRESET_VERSION;
  before["shakeAction"] = 2;
  ok = expect(settings.fromJson(before.as<JsonVariantConst>()), "file from before the rows loads") && ok;
  ok = expect(settings.faceDownAction == 0 && settings.faceUpAction == 0 && settings.shakeAction == 2,
              "a file without the keys keeps both Off and its shake") &&
       ok;
  const uint8_t count = static_cast<uint8_t>(std::size(quickaction::SHAKE_ORDER));
  for (uint8_t a = 0; a < count; ++a) {
    settings.faceDownAction = a;
    settings.faceUpAction = static_cast<uint8_t>(count - 1 - a);
    JsonDocument saved;
    settings.toJson(saved);
    settings.faceDownAction = settings.faceUpAction = 0;
    ok = expect(settings.fromJson(saved.as<JsonVariantConst>()), "face JSON loads") && ok;
    ok = expect(settings.faceDownAction == a && settings.faceUpAction == count - 1 - a, "every action round trips") &&
         ok;
  }
  settings.faceDownAction = settings.faceUpAction = 0;
  JsonDocument corrupt;
  corrupt["tenorPresetVersion"] = CrossPointSettings::TENOR_PRESET_VERSION;
  corrupt["faceDownAction"] = count;
  corrupt["faceUpAction"] = 200;
  ok = expect(settings.fromJson(corrupt.as<JsonVariantConst>()), "corrupt face JSON loads") && ok;
  ok = expect(settings.faceDownAction == 0 && settings.faceUpAction == 0, "out-of-range values keep Off") && ok;
  std::printf("menu_tilt_settings=flip:%s\n", ok ? "GREEN" : "RED");
  return ok ? 0 : 1;
}

// Double tap on the back, the screen, then the edge: the last Gestures rows, after face up, the
// shake's own list, Off on a new card and on a file from before them, every value saved and read
// back, out-of-range Off.
int runDoubleTapSettings() {
  halTiltSensor.available = true;
  CrossPointSettings& settings = SETTINGS;
  bool ok = expect(settings.doubleTapAction == 0, "a new card starts with double tap Off");
  const auto& catalog = getBaseSettingsList();
  const SettingInfo* up = findSetting(catalog, "faceUpAction");
  const SettingInfo* tap = findSetting(catalog, "doubleTapAction");
  ok = expect(up && tap, "the row is present with the sensor") && ok;
  if (!up || !tap) return 1;
  ok = expect(tap == up + 1, "double tap right after face up") && ok;
  const SettingInfo* screen = findSetting(catalog, "screenTapAction");
  ok = expect(screen == tap + 1 && screen->type == SettingType::ENUM && screen->category == StrId::STR_CAT_MOTION &&
                  screen->nameId == StrId::STR_SCREEN_TAP_ACTION &&
                  screen->valuePtr == &CrossPointSettings::screenTapAction &&
                  screen->enumValues == quickaction::shakeLabels(),
              "the screen row right after the back, with the shake's choices") &&
       ok;
  const SettingInfo* edge = findSetting(catalog, "edgeTapAction");
  ok = expect(edge == screen + 1 && edge->type == SettingType::ENUM && edge->category == StrId::STR_CAT_MOTION &&
                  edge->nameId == StrId::STR_EDGE_TAP_ACTION && edge->valuePtr == &CrossPointSettings::edgeTapAction &&
                  edge->enumValues == quickaction::shakeLabels(),
              "the edge row right after the screen, with the shake's choices") &&
       ok;
  ok = expect(tap->type == SettingType::ENUM && tap->category == StrId::STR_CAT_MOTION &&
                  tap->nameId == StrId::STR_DOUBLE_TAP_ACTION && tap->valuePtr == &CrossPointSettings::doubleTapAction &&
                  tap->enumValues == quickaction::shakeLabels(),
              "double tap lists the shake's choices") &&
       ok;

  JsonDocument before;
  before["tenorPresetVersion"] = CrossPointSettings::TENOR_PRESET_VERSION;
  before["faceUpAction"] = 3;
  ok = expect(settings.fromJson(before.as<JsonVariantConst>()), "file from before the row loads") && ok;
  ok = expect(settings.doubleTapAction == 0 && settings.screenTapAction == 0 && settings.edgeTapAction == 0 &&
                  settings.faceUpAction == 3,
              "a file without the keys keeps every double tap Off and its face up") &&
       ok;
  const uint8_t count = static_cast<uint8_t>(std::size(quickaction::SHAKE_ORDER));
  for (uint8_t a = 0; a < count; ++a) {
    settings.doubleTapAction = a;
    settings.screenTapAction = static_cast<uint8_t>((a + 1) % count);
    settings.edgeTapAction = static_cast<uint8_t>(count - 1 - a);
    JsonDocument saved;
    settings.toJson(saved);
    settings.doubleTapAction = 0;
    settings.screenTapAction = 0;
    settings.edgeTapAction = 0;
    ok = expect(settings.fromJson(saved.as<JsonVariantConst>()), "double tap JSON loads") && ok;
    ok = expect(settings.doubleTapAction == a && settings.screenTapAction == (a + 1) % count &&
                    settings.edgeTapAction == count - 1 - a,
                "every action round trips") &&
         ok;
  }
  settings.doubleTapAction = 0;
  settings.screenTapAction = 0;
  settings.edgeTapAction = 0;
  JsonDocument corrupt;
  corrupt["tenorPresetVersion"] = CrossPointSettings::TENOR_PRESET_VERSION;
  corrupt["doubleTapAction"] = count;
  corrupt["screenTapAction"] = count;
  corrupt["edgeTapAction"] = 200;
  ok = expect(settings.fromJson(corrupt.as<JsonVariantConst>()), "corrupt double tap JSON loads") && ok;
  ok = expect(settings.doubleTapAction == 0 && settings.screenTapAction == 0 && settings.edgeTapAction == 0,
              "an out-of-range value keeps Off") &&
       ok;
  settings.faceUpAction = 0;
  std::printf("menu_tilt_settings=double-tap:%s\n", ok ? "GREEN" : "RED");
  return ok ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  if (std::strcmp(argv[1], "power") == 0) return runPowerWake();
  if (std::strcmp(argv[1], "action") == 0) return runQuickActions();
  if (std::strcmp(argv[1], "shake") == 0) return runShakeSettings();
  if (std::strcmp(argv[1], "double-tap") == 0) return runDoubleTapSettings();
  if (std::strcmp(argv[1], "flip") == 0) return runFlipSettings();
  if (std::strcmp(argv[1], "power-values") == 0) return runPowerValuesKept();
  const bool hasImu = std::strcmp(argv[1], "imu") == 0;
  if (!hasImu && std::strcmp(argv[1], "no-imu") != 0) return 2;

  halTiltSensor.available = hasImu;
  const auto& catalog = getBaseSettingsList();
  const SettingInfo* readerTilt = findSetting(catalog, "tiltPageTurn");
  const SettingInfo* menuTilt = findSetting(catalog, "tiltTabNavigation");
  const SettingInfo* rowTilt = findSetting(catalog, "tiltMenuNavigation");
  CrossPointSettings& settings = SETTINGS;
  const SettingInfo* strengthH = findSetting(catalog, "tiltStrengthH");
  const SettingInfo* strengthV = findSetting(catalog, "tiltStrengthV");
  // 1.6.5 added 11 rows (library metadata, time zone, DST, clock in header, Back to file
  // browser, three Home button actions, two page gestures, double-click light); every
  // existing row kept its place. v1.0.52 then dropped the UI theme row and added the starting up notice row.
  bool ok = expect(catalog.size() == (hasImu ? 96U : 84U), "X3 descriptor count");
  ok = expect(longPressValuesMatch(catalog, hasImu), "Confirm-hold list shows Reader menu and appends the new actions") &&
       ok;
  ok = longPressStoreKept(catalog, hasImu) && ok;

  if (!hasImu) {
    ok = expect(readerTilt == nullptr && menuTilt == nullptr && rowTilt == nullptr && strengthH == nullptr &&
                    strengthV == nullptr,
                "tilt descriptors require IMU") &&
         ok;
    settings.tiltTabNavigation = CrossPointSettings::TILT_OFF;
    JsonDocument injected;
    injected["tenorPresetVersion"] = CrossPointSettings::TENOR_PRESET_VERSION;
    injected["tiltTabNavigation"] = CrossPointSettings::TILT_NORMAL;
    ok = expect(settings.fromJson(injected.as<JsonVariantConst>()), "non-IMU JSON loads") && ok;
    ok = expect(settings.tiltTabNavigation == CrossPointSettings::TILT_OFF, "non-IMU JSON stays default") && ok;
    JsonDocument saved;
    settings.toJson(saved);
    ok = expect(saved["tiltTabNavigation"].isNull() && saved["tiltMenuNavigation"].isNull(),
                "non-IMU JSON omits both menu tilt keys") &&
         ok;
    ok = expect(findSetting(catalog, "shakeAction") == nullptr && findSetting(catalog, "shakeStrength") == nullptr &&
                    saved["shakeAction"].isNull() && saved["shakeStrength"].isNull(),
                "no shake rows and no shake keys without the sensor") &&
         ok;
    ok = expect(findSetting(catalog, "faceDownAction") == nullptr && findSetting(catalog, "faceUpAction") == nullptr &&
                    saved["faceDownAction"].isNull() && saved["faceUpAction"].isNull(),
                "no face down or face up rows and keys without the sensor") &&
         ok;
    ok = expect(findSetting(catalog, "doubleTapAction") == nullptr && saved["doubleTapAction"].isNull() &&
                    findSetting(catalog, "screenTapAction") == nullptr && saved["screenTapAction"].isNull() &&
                    findSetting(catalog, "edgeTapAction") == nullptr && saved["edgeTapAction"].isNull(),
                "no double tap rows and keys without the sensor") &&
         ok;
    ok = expect(std::none_of(catalog.begin(), catalog.end(),
                             [](const SettingInfo& s) { return s.category == StrId::STR_CAT_MOTION; }),
                "no Gestures row without the sensor") &&
         ok;
    std::printf("menu_tilt_settings=no-imu:%s\n", ok ? "GREEN" : "RED");
    return ok ? 0 : 1;
  }

  ok = expect(readerTilt && menuTilt && rowTilt, "all three tilt descriptors present") && ok;
  if (!readerTilt || !menuTilt || !rowTilt) return 1;
  ok = expect(readerTilt->nameId == StrId::STR_TILT_PAGE_TURN &&
                  readerTilt->valuePtr == &CrossPointSettings::tiltPageTurn && isTiltEnum(*readerTilt, StrId::STR_CAT_MOTION),
              "reader tilt descriptor keeps its key and values, in Gestures") &&
       ok;
  ok = expect(menuTilt->nameId == StrId::STR_TILT_TAB_NAVIGATION &&
                  menuTilt->valuePtr == &CrossPointSettings::tiltTabNavigation && isTiltEnum(*menuTilt, StrId::STR_CAT_MOTION),
              "tab tilt descriptor uses independent field") &&
       ok;
  ok = expect(rowTilt->nameId == StrId::STR_TILT_MENU_NAVIGATION &&
                  rowTilt->valuePtr == &CrossPointSettings::tiltMenuNavigation &&
                  isTiltEnum(*rowTilt, StrId::STR_CAT_MOTION),
              "row tilt descriptor uses independent field in Gestures") &&
       ok;
  ok = expect(menuTilt == readerTilt + 1, "tab tilt follows reader tilt") && ok;
  // Every sensor row, in one tab and this order; none left in Reader or Controls.
  {
    std::vector<std::string> motion;
    for (const auto& s : catalog) {
      if (s.category == StrId::STR_CAT_MOTION && s.key) motion.emplace_back(s.key);
    }
    ok = expect(motion == std::vector<std::string>{"tiltPageTurn", "tiltTabNavigation", "tiltMenuNavigation",
                                                   "tiltStrengthH", "tiltStrengthV", "shakeAction", "shakeStrength",
                                                   "faceDownAction", "faceUpAction", "doubleTapAction",
                                                   "screenTapAction", "edgeTapAction"},
                "Gestures holds every sensor row, in order") &&
         ok;
  }
  ok = expect(rowTilt == menuTilt + 1, "row tilt row sits directly after tab tilt") && ok;
  ok = expect(settings.tiltMenuNavigation == CrossPointSettings::TILT_NORMAL, "row tilt starts on the setup") && ok;

  // Strength rows follow the tilt rows, one per axis.
  ok = expect(strengthH && strengthV, "both strength descriptors present") && ok;
  if (strengthH && strengthV) {
  ok = expect(isStrengthEnum(*strengthH, StrId::STR_TILT_STRENGTH_H, &CrossPointSettings::tiltStrengthH) &&
                  isStrengthEnum(*strengthV, StrId::STR_TILT_STRENGTH_V, &CrossPointSettings::tiltStrengthV),
              "strength descriptors use their own fields, labels and Light/Medium/Strong") &&
       ok;
  ok = expect(strengthH == rowTilt + 1 && strengthV == rowTilt + 2, "strength rows sit right after row tilt") && ok;
  ok = expect(settings.tiltStrengthH == CrossPointSettings::TILT_STRENGTH_MEDIUM &&
                  settings.tiltStrengthV == CrossPointSettings::TILT_STRENGTH_LIGHT,
              "side strength starts Medium, up/down Light as the tenor/cross setup") &&
       ok;
  {
    JsonDocument input;
    input["tenorPresetVersion"] = CrossPointSettings::TENOR_PRESET_VERSION;
    input["tiltStrengthH"] = CrossPointSettings::TILT_STRENGTH_LIGHT;
    input["tiltStrengthV"] = CrossPointSettings::TILT_STRENGTH_STRONG;
    ok = expect(settings.fromJson(input.as<JsonVariantConst>()), "strength JSON loads") && ok;
    JsonDocument saved;
    settings.toJson(saved);
    ok = expect((saved["tiltStrengthH"] | uint8_t{255}) == CrossPointSettings::TILT_STRENGTH_LIGHT &&
                    (saved["tiltStrengthV"] | uint8_t{255}) == CrossPointSettings::TILT_STRENGTH_STRONG,
                "each axis strength round trips on its own") &&
         ok;
    settings.tiltStrengthH = CrossPointSettings::TILT_STRENGTH_MEDIUM;
    settings.tiltStrengthV = CrossPointSettings::TILT_STRENGTH_MEDIUM;
    JsonDocument corrupt;
    corrupt["tenorPresetVersion"] = CrossPointSettings::TENOR_PRESET_VERSION;
    corrupt["tiltStrengthH"] = CrossPointSettings::TILT_STRENGTH_COUNT;
    corrupt["tiltStrengthV"] = 200;
    ok = expect(settings.fromJson(corrupt.as<JsonVariantConst>()), "corrupt strength JSON loads") && ok;
    ok = expect(settings.tiltStrengthH == CrossPointSettings::TILT_STRENGTH_MEDIUM &&
                    settings.tiltStrengthV == CrossPointSettings::TILT_STRENGTH_MEDIUM,
                "out-of-range strength keeps Medium") &&
         ok;
  }
  }

  // The quick toggle turns tilt off, then back on to the mode that was on.
  settings.tiltPageTurn = CrossPointSettings::TILT_OFF;
  settings.tiltPageTurnLastOn = CrossPointSettings::TILT_NORMAL;
  settings.toggleTiltPageTurn();
  ok = expect(settings.tiltPageTurn == CrossPointSettings::TILT_NORMAL, "toggle from Off defaults to Normal") && ok;
  settings.tiltPageTurn = CrossPointSettings::TILT_NVERTED;
  settings.toggleTiltPageTurn();
  ok = expect(settings.tiltPageTurn == CrossPointSettings::TILT_OFF, "toggle turns Inverted off") && ok;
  {
    JsonDocument saved;
    settings.toJson(saved);
    settings.tiltPageTurnLastOn = CrossPointSettings::TILT_NORMAL;
    ok = expect(settings.fromJson(saved.as<JsonVariantConst>()), "toggle state JSON loads") && ok;
  }
  settings.toggleTiltPageTurn();
  ok = expect(settings.tiltPageTurn == CrossPointSettings::TILT_NVERTED,
              "toggle brings Inverted back, across a settings reload") &&
       ok;

  settings.tiltPageTurn = CrossPointSettings::TILT_OFF;
  settings.tiltTabNavigation = CrossPointSettings::TILT_OFF;
  settings.tiltMenuNavigation = CrossPointSettings::TILT_OFF;
  JsonDocument oldJson;
  oldJson["tenorPresetVersion"] = CrossPointSettings::TENOR_PRESET_VERSION;
  oldJson["tiltPageTurn"] = CrossPointSettings::TILT_NVERTED;
  ok = expect(settings.fromJson(oldJson.as<JsonVariantConst>()), "old settings JSON loads") && ok;
  ok = expect(settings.tiltPageTurn == CrossPointSettings::TILT_NVERTED &&
                  settings.tiltTabNavigation == CrossPointSettings::TILT_OFF &&
                  settings.tiltMenuNavigation == CrossPointSettings::TILT_OFF,
              "missing tab and row keys default off without changing reader") &&
       ok;

  for (uint8_t readerMode = CrossPointSettings::TILT_OFF; readerMode < CrossPointSettings::TILT_PAGE_TURN_COUNT;
       ++readerMode) {
    for (uint8_t menuMode = CrossPointSettings::TILT_OFF; menuMode < CrossPointSettings::TILT_PAGE_TURN_COUNT;
         ++menuMode) {
      const uint8_t rowMode = static_cast<uint8_t>((readerMode + menuMode) % CrossPointSettings::TILT_PAGE_TURN_COUNT);
      settings.tiltPageTurn = CrossPointSettings::TILT_OFF;
      settings.tiltTabNavigation = CrossPointSettings::TILT_OFF;
      settings.tiltMenuNavigation = CrossPointSettings::TILT_OFF;
      JsonDocument input;
      input["tenorPresetVersion"] = CrossPointSettings::TENOR_PRESET_VERSION;
      input["tiltPageTurn"] = readerMode;
      input["tiltTabNavigation"] = menuMode;
      input["tiltMenuNavigation"] = rowMode;
      ok = expect(settings.fromJson(input.as<JsonVariantConst>()), "independent tilt JSON loads") && ok;
      ok = expect(settings.tiltPageTurn == readerMode && settings.tiltTabNavigation == menuMode &&
                      settings.tiltMenuNavigation == rowMode,
                  "reader, tab and row modes remain independent") &&
           ok;
      JsonDocument saved;
      settings.toJson(saved);
      ok = expect((saved["tiltPageTurn"] | uint8_t{255}) == readerMode &&
                      (saved["tiltTabNavigation"] | uint8_t{255}) == menuMode &&
                      (saved["tiltMenuNavigation"] | uint8_t{255}) == rowMode,
                  "each explicit mode round trips") &&
           ok;
    }
  }

  settings.tiltPageTurn = CrossPointSettings::TILT_OFF;
  settings.tiltTabNavigation = CrossPointSettings::TILT_OFF;
  settings.tiltMenuNavigation = CrossPointSettings::TILT_OFF;
  JsonDocument corruptJson;
  corruptJson["tenorPresetVersion"] = CrossPointSettings::TENOR_PRESET_VERSION;
  corruptJson["tiltPageTurn"] = CrossPointSettings::TILT_NORMAL;
  corruptJson["tiltTabNavigation"] = CrossPointSettings::TILT_PAGE_TURN_COUNT;
  corruptJson["tiltMenuNavigation"] = CrossPointSettings::TILT_PAGE_TURN_COUNT;
  ok = expect(settings.fromJson(corruptJson.as<JsonVariantConst>()), "out-of-range tab tilt JSON loads") && ok;
  ok = expect(settings.tiltPageTurn == CrossPointSettings::TILT_NORMAL &&
                  settings.tiltTabNavigation == CrossPointSettings::TILT_OFF &&
                  settings.tiltMenuNavigation == CrossPointSettings::TILT_OFF,
              "out-of-range tab and row modes clamp without changing reader") &&
       ok;

  std::printf("menu_tilt_settings=imu:%s\n", ok ? "GREEN" : "RED");
  return ok ? 0 : 1;
}
