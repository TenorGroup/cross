#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

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
  return setting.type == SettingType::ENUM && setting.category == StrId::STR_CAT_CONTROLS && setting.nameId == name &&
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
  return longPress->enumValues == expected &&
         expected.size() == (hasImu ? CrossPointSettings::LONG_PRESS_MENU_FUNCTION_COUNT
                                    : CrossPointSettings::LONG_PRESS_MENU_FUNCTION_COUNT - 1U);
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

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  if (std::strcmp(argv[1], "power") == 0) return runPowerWake();
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
  bool ok = expect(catalog.size() == (hasImu ? 75U : 70U), "X3 descriptor count");
  ok = expect(longPressValuesMatch(catalog, hasImu), "Confirm-hold list shows Reader menu and appends the new actions") &&
       ok;

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
    std::printf("menu_tilt_settings=no-imu:%s\n", ok ? "GREEN" : "RED");
    return ok ? 0 : 1;
  }

  ok = expect(readerTilt && menuTilt && rowTilt, "all three tilt descriptors present") && ok;
  if (!readerTilt || !menuTilt || !rowTilt) return 1;
  ok = expect(readerTilt->nameId == StrId::STR_TILT_PAGE_TURN &&
                  readerTilt->valuePtr == &CrossPointSettings::tiltPageTurn && isTiltEnum(*readerTilt, StrId::STR_CAT_READER),
              "reader tilt descriptor stays unchanged") &&
       ok;
  ok = expect(menuTilt->nameId == StrId::STR_TILT_TAB_NAVIGATION &&
                  menuTilt->valuePtr == &CrossPointSettings::tiltTabNavigation && isTiltEnum(*menuTilt, StrId::STR_CAT_CONTROLS),
              "tab tilt descriptor uses independent field") &&
       ok;
  ok = expect(rowTilt->nameId == StrId::STR_TILT_MENU_NAVIGATION &&
                  rowTilt->valuePtr == &CrossPointSettings::tiltMenuNavigation &&
                  isTiltEnum(*rowTilt, StrId::STR_CAT_CONTROLS),
              "row tilt descriptor uses independent field in Controls") &&
       ok;
  ok = expect(rowTilt == menuTilt + 1, "row tilt row sits directly after tab tilt") && ok;
  ok = expect(settings.tiltMenuNavigation == CrossPointSettings::TILT_NORMAL, "row tilt starts on the setup") && ok;

  // Strength rows follow the tilt rows in Controls, one per axis.
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
