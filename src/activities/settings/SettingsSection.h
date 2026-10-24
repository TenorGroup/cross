#pragma once

#include <I18n.h>

#include <cstddef>
#include <cstdint>

#include "SettingsTabs.h"

// Top-level groups on the Settings screen, in display order. End doubles as
// "no section" where one is optional.
enum class SettingsSection : uint8_t { General, Display, Reader, Controls, Library, Network, End };

inline constexpr size_t SETTINGS_SECTION_COUNT = static_cast<size_t>(SettingsSection::End);

inline constexpr StrId SETTINGS_SECTION_TITLES[SETTINGS_SECTION_COUNT] = {
    StrId::STR_SETTINGS_GENERAL, StrId::STR_CAT_DISPLAY, StrId::STR_CAT_READER,
    StrId::STR_CAT_CONTROLS,     StrId::STR_LIBRARY,     StrId::STR_SETTINGS_NETWORK,
};

// tenor keeps its own Settings frame (tabs), not upstream's section list. This is the tab that holds what an
// upstream section holds, for callers that name a section (the silent restart back into the Wi-Fi rows).
inline int settingsTabForSection(const SettingsSection section) {
  switch (section) {
    case SettingsSection::Display:
      return static_cast<int>(settingstabs::Tab::SCREEN);
    case SettingsSection::Reader:
      return static_cast<int>(settingstabs::Tab::READER);
    case SettingsSection::Controls:
      return static_cast<int>(settingstabs::Tab::CONTROLS);
    case SettingsSection::Network:
      return static_cast<int>(settingstabs::Tab::DEVICE);  // Wi-Fi and Bluetooth rows
    default:
      return static_cast<int>(settingstabs::Tab::SYSTEM);
  }
}
