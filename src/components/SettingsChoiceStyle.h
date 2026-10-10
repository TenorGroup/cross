#pragma once

enum class SettingsChoiceStyle { Inline, Popup, Page, None };
constexpr int SETTINGS_CHOICE_PAGE_THRESHOLD = 7;

constexpr SettingsChoiceStyle settingsChoiceStyle(const int count, const bool, const bool onOff = false) {
  if (onOff) return SettingsChoiceStyle::Inline;
  if (count < 2) return SettingsChoiceStyle::None;
  return count >= SETTINGS_CHOICE_PAGE_THRESHOLD ? SettingsChoiceStyle::Page
                                                              : SettingsChoiceStyle::Popup;
}

template <typename Setting>
auto settingsChoiceStyle(const Setting& setting, const bool touchShell)
    -> decltype(setting.enumLabels(), SettingsChoiceStyle{}) {
  using Type = decltype(setting.type);
  const auto labels = setting.enumLabels();
  using Label = typename decltype(labels)::value_type;
  const bool states = setting.type == Type::ENUM && setting.enumStringValues.empty() && labels.size() == 2 &&
      ((labels[0] == Label::STR_STATE_OFF && labels[1] == Label::STR_STATE_ON) ||
       (labels[0] == Label::STR_STATE_ON && labels[1] == Label::STR_STATE_OFF));
  const int count = setting.type == Type::TOGGLE ? 2 : setting.type == Type::ENUM
      ? static_cast<int>(setting.enumStringValues.empty() ? labels.size() : setting.enumStringValues.size())
      : setting.type == Type::VALUE
          ? setting.valueRange.step ? (setting.valueRange.max - setting.valueRange.min) / setting.valueRange.step + 1 : 0 : 0;
  return settingsChoiceStyle(count, touchShell, setting.type == Type::TOGGLE || states);
}
