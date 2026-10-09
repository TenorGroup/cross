#pragma once

enum class SettingsChoiceStyle { Inline, Popup, Page };

constexpr int SETTINGS_CHOICE_PAGE_THRESHOLD = 7;

constexpr SettingsChoiceStyle settingsChoiceStyle(const int count, const bool touchShell) {
  if (count <= 2) return SettingsChoiceStyle::Inline;
  return touchShell && count >= SETTINGS_CHOICE_PAGE_THRESHOLD ? SettingsChoiceStyle::Page
                                                              : SettingsChoiceStyle::Popup;
}
