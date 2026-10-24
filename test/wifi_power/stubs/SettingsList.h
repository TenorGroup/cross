#pragma once
// The settings catalog the Wi-Fi screen frees on entry (SettingsList.h): counted, not built.
inline int releasedSettingsCatalogs = 0;
inline void releaseBaseSettingsList() { ++releasedSettingsCatalogs; }
