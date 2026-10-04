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
    expect(settingOpensNext(setting), "every action must open the next screen");
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
  const std::array<StrId, 5> labels = {StrId::STR_NONE_OPT, StrId::STR_NONE_OPT, StrId::STR_NONE_OPT,
                                      StrId::STR_NONE_OPT, StrId::STR_NONE_OPT};
  for (size_t count = 0; count <= labels.size(); count++) {
    auto dynamicLabels = SettingInfo::Enum(StrId::STR_NONE_OPT, &CrossPointSettings::value,
                                          std::vector<StrId>(count, StrId::STR_NONE_OPT));
    expect(settingOpensNext(dynamicLabels) == (count >= 4), "stored enum vector uses the picker threshold");

    auto staticLabels = baseSetting(SettingType::ENUM);
    staticLabels.valuePtr = &CrossPointSettings::value;
    staticLabels.staticEnumValues = std::span<const StrId>(labels.data(), count);
    expect(settingOpensNext(staticLabels) == (count >= 4), "stored static enum uses the picker threshold");
  }

  auto labelsWin = SettingInfo::Enum(StrId::STR_NONE_OPT, &CrossPointSettings::value,
                                    std::vector<StrId>(3, StrId::STR_NONE_OPT));
  labelsWin.enumStringValues.assign(5, "value");
  expect(!settingOpensNext(labelsWin), "stored enum ignores runtime strings");
}

SettingInfo accessorEnum(const size_t labelCount, const size_t stringCount) {
  auto setting = SettingInfo::DynamicEnum(StrId::STR_NONE_OPT,
                                         std::vector<StrId>(labelCount, StrId::STR_NONE_OPT), [] { return uint8_t{0}; },
                                         [](uint8_t) {});
  setting.enumStringValues.assign(stringCount, "value");
  return setting;
}

void checkAccessorEnums() {
  for (size_t count = 0; count <= 5; count++) {
    expect(settingOpensNext(accessorEnum(count, 0)) == (count >= 4),
           "accessor enum falls back to translated labels");
    expect(settingOpensNext(accessorEnum(0, count)) == (count >= 4),
           "accessor enum uses runtime strings");
  }
  expect(settingOpensNext(accessorEnum(3, 5)), "runtime strings take precedence for accessor enums");
  expect(!settingOpensNext(accessorEnum(5, 3)), "short runtime strings take precedence for accessor enums");
}

}  // namespace

int main() {
  checkActions();
  checkDirectValues();
  checkStoredEnums();
  checkAccessorEnums();
  return failures == 0 ? 0 : 1;
}
