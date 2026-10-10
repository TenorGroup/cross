#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "activities/settings/SettingsActivity.h"

namespace {

int failures = 0;

void expect(const bool condition, const char* const message) {
  if (condition) return;
  std::fprintf(stderr, "FAIL: %s\n", message);
  failures++;
}

SettingInfo baseSetting(const SettingType type, const StrId nameId = StrId::STR_NONE_OPT) {
  SettingInfo setting;
  setting.nameId = nameId;
  setting.type = type;
  return setting;
}

void checkActions() {
  for (int value = static_cast<int>(SettingAction::None) + 1; value <= static_cast<int>(SettingAction::About); value++) {
    const auto setting = SettingInfo::Action(StrId::STR_NONE_OPT, static_cast<SettingAction>(value));
    expect(settingOpensNext(setting) == (setting.action != SettingAction::Language || getLanguageCount() >= 7),
           "actions open their destination or the language popup");
  }
  expect(!settingOpensNext(SettingInfo::Action(StrId::STR_DISPLAY_CHIP, SettingAction::None)),
         "read-only panel chip has no next screen");
  expect(settingOpensNext(SettingInfo::Action(StrId::STR_NONE_OPT, SettingAction::HomeButton)),
         "HomeButton opens its settings screen");
}

void checkDirectValues() {
  expect(!settingOpensNext(SettingInfo::Toggle(StrId::STR_NONE_OPT, &CrossPointSettings::value)),
         "toggle changes in place");
  expect(!settingOpensNext(
             SettingInfo::Value(StrId::STR_NONE_OPT, &CrossPointSettings::value, SettingInfo::ValueRange{0, 5, 1})),
         "ordinary value changes in place");
  expect(settingOpensNext(SettingInfo::Value(StrId::STR_TIME_TO_SLEEP, &CrossPointSettings::value,
                                            SettingInfo::ValueRange{0, 5, 1})),
         "sleep time opens its picker");
  expect(!settingOpensNext(baseSetting(SettingType::STRING)), "string changes in place");
}

void checkStoredEnums() {
  const std::array<StrId, 8> labels = {StrId::STR_NONE_OPT, StrId::STR_NONE_OPT, StrId::STR_NONE_OPT,
                                      StrId::STR_NONE_OPT, StrId::STR_NONE_OPT};
  for (size_t count = 0; count <= labels.size(); count++) {
    auto dynamicLabels = SettingInfo::Enum(StrId::STR_NONE_OPT, &CrossPointSettings::value,
                                          std::vector<StrId>(count, StrId::STR_NONE_OPT));
    expect(settingOpensNext(dynamicLabels) == (count >= 7), "stored enum vector opens a page from 7 values");

    auto staticLabels = baseSetting(SettingType::ENUM);
    staticLabels.valuePtr = &CrossPointSettings::value;
    staticLabels.staticEnumValues = std::span<const StrId>(labels.data(), count);
    expect(settingOpensNext(staticLabels) == (count >= 7), "stored static enum opens a page from 7 values");
  }

  auto labelsWin = SettingInfo::Enum(StrId::STR_NONE_OPT, &CrossPointSettings::value,
                                    std::vector<StrId>(3, StrId::STR_NONE_OPT));
  labelsWin.enumStringValues.assign(5, "value");
  expect(!settingOpensNext(labelsWin), "5 runtime strings use a popup");
  labelsWin.enumStringValues.assign(2, "value");
  expect(!settingOpensNext(labelsWin), "2 runtime strings use a popup");
  const auto states = SettingInfo::Enum(StrId::STR_NONE_OPT, &CrossPointSettings::value,
                                      {StrId::STR_STATE_OFF, StrId::STR_STATE_ON});
  expect(!settingOpensNext(states), "off/on enum changes in place");
}

SettingInfo accessorEnum(const size_t labelCount, const size_t stringCount) {
  auto setting = SettingInfo::DynamicEnum(StrId::STR_NONE_OPT,
                                         std::vector<StrId>(labelCount, StrId::STR_NONE_OPT), [] { return uint8_t{0}; },
                                         [](uint8_t) {});
  setting.enumStringValues.assign(stringCount, "value");
  return setting;
}

void checkAccessorEnums() {
  for (size_t count = 0; count <= 8; count++) {
    expect(settingOpensNext(accessorEnum(count, 0)) == (count >= 7),
           "accessor enum falls back to translated labels");
    expect(settingOpensNext(accessorEnum(0, count)) == (count >= 7),
           "accessor enum uses runtime strings");
  }
  expect(!settingOpensNext(accessorEnum(3, 5)), "5 runtime strings use a popup for accessor enums");
  expect(!settingOpensNext(accessorEnum(5, 3)), "3 runtime strings use a popup");
}

}  // namespace

int main() {
  checkActions();
  checkDirectValues();
  checkStoredEnums();
  checkAccessorEnums();
  return failures == 0 ? 0 : 1;
}
