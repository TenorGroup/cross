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

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  const bool hasImu = std::strcmp(argv[1], "imu") == 0;
  if (!hasImu && std::strcmp(argv[1], "no-imu") != 0) return 2;

  halTiltSensor.available = hasImu;
  const auto& catalog = getBaseSettingsList();
  const SettingInfo* readerTilt = findSetting(catalog, "tiltPageTurn");
  const SettingInfo* menuTilt = findSetting(catalog, "tiltTabNavigation");
  CrossPointSettings& settings = SETTINGS;
  bool ok = expect(catalog.size() == (hasImu ? 72U : 70U), "X3 descriptor count");

  if (!hasImu) {
    ok = expect(readerTilt == nullptr && menuTilt == nullptr, "tilt descriptors require IMU") && ok;
    settings.tiltTabNavigation = CrossPointSettings::TILT_OFF;
    JsonDocument injected;
    injected["tiltTabNavigation"] = CrossPointSettings::TILT_NORMAL;
    ok = expect(settings.fromJson(injected.as<JsonVariantConst>()), "non-IMU JSON loads") && ok;
    ok = expect(settings.tiltTabNavigation == CrossPointSettings::TILT_OFF, "non-IMU JSON stays default") && ok;
    JsonDocument saved;
    settings.toJson(saved);
    ok = expect(saved["tiltTabNavigation"].isNull(), "non-IMU JSON omits tab tilt") && ok;
    std::printf("menu_tilt_settings=no-imu:%s\n", ok ? "GREEN" : "RED");
    return ok ? 0 : 1;
  }

  ok = expect(readerTilt && menuTilt, "both tilt descriptors present") && ok;
  if (!readerTilt || !menuTilt) return 1;
  ok = expect(readerTilt->nameId == StrId::STR_TILT_PAGE_TURN &&
                  readerTilt->valuePtr == &CrossPointSettings::tiltPageTurn && isTiltEnum(*readerTilt, StrId::STR_CAT_READER),
              "reader tilt descriptor stays unchanged") &&
       ok;
  ok = expect(menuTilt->nameId == StrId::STR_TILT_TAB_NAVIGATION &&
                  menuTilt->valuePtr == &CrossPointSettings::tiltTabNavigation && isTiltEnum(*menuTilt, StrId::STR_CAT_CONTROLS),
              "tab tilt descriptor uses independent field") &&
       ok;

  settings.tiltPageTurn = CrossPointSettings::TILT_OFF;
  settings.tiltTabNavigation = CrossPointSettings::TILT_OFF;
  JsonDocument oldJson;
  oldJson["tiltPageTurn"] = CrossPointSettings::TILT_NVERTED;
  ok = expect(settings.fromJson(oldJson.as<JsonVariantConst>()), "old settings JSON loads") && ok;
  ok = expect(settings.tiltPageTurn == CrossPointSettings::TILT_NVERTED &&
                  settings.tiltTabNavigation == CrossPointSettings::TILT_OFF,
              "missing tab key defaults off without changing reader") &&
       ok;

  for (uint8_t readerMode = CrossPointSettings::TILT_OFF; readerMode < CrossPointSettings::TILT_PAGE_TURN_COUNT;
       ++readerMode) {
    for (uint8_t menuMode = CrossPointSettings::TILT_OFF; menuMode < CrossPointSettings::TILT_PAGE_TURN_COUNT;
         ++menuMode) {
      settings.tiltPageTurn = CrossPointSettings::TILT_OFF;
      settings.tiltTabNavigation = CrossPointSettings::TILT_OFF;
      JsonDocument input;
      input["tiltPageTurn"] = readerMode;
      input["tiltTabNavigation"] = menuMode;
      ok = expect(settings.fromJson(input.as<JsonVariantConst>()), "independent tilt JSON loads") && ok;
      ok = expect(settings.tiltPageTurn == readerMode && settings.tiltTabNavigation == menuMode,
                  "reader and menu modes remain independent") &&
           ok;
      JsonDocument saved;
      settings.toJson(saved);
      ok = expect((saved["tiltPageTurn"] | uint8_t{255}) == readerMode &&
                      (saved["tiltTabNavigation"] | uint8_t{255}) == menuMode,
                  "each explicit mode round trips") &&
           ok;
    }
  }

  settings.tiltPageTurn = CrossPointSettings::TILT_OFF;
  settings.tiltTabNavigation = CrossPointSettings::TILT_OFF;
  JsonDocument corruptJson;
  corruptJson["tiltPageTurn"] = CrossPointSettings::TILT_NORMAL;
  corruptJson["tiltTabNavigation"] = CrossPointSettings::TILT_PAGE_TURN_COUNT;
  ok = expect(settings.fromJson(corruptJson.as<JsonVariantConst>()), "out-of-range tab tilt JSON loads") && ok;
  ok = expect(settings.tiltPageTurn == CrossPointSettings::TILT_NORMAL &&
                  settings.tiltTabNavigation == CrossPointSettings::TILT_OFF,
              "out-of-range tab mode clamps without changing reader") &&
       ok;

  std::printf("menu_tilt_settings=imu:%s\n", ok ? "GREEN" : "RED");
  return ok ? 0 : 1;
}
