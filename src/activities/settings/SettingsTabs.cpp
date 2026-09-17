#include "SettingsTabs.h"

namespace settingstabs {

StrId tenThe(const Tab tab) {
  switch (tab) {
    case Tab::SCREEN:
      return StrId::STR_CAT_DISPLAY;
    case Tab::READER:
      return StrId::STR_CAT_READER;
    case Tab::CONTROLS:
      return StrId::STR_CAT_CONTROLS;
    case Tab::SYSTEM:
      return StrId::STR_CAT_SYSTEM;
    case Tab::DEVICE:
      return StrId::STR_CAT_DEVICE;
    case Tab::OTHER:
      return StrId::STR_CAT_OTHER;
    case Tab::KEYBOARD:
      return StrId::STR_CAT_KEYBOARD;
  }
  return StrId::STR_CAT_SYSTEM;
}

Tab nhaCua(const Action action) {
  switch (action) {
    case Action::TextSettings:
    case Action::DownloadFonts:
    case Action::CustomiseStatusBar:
      return Tab::READER;
    case Action::RemapFrontButtons:
      return Tab::CONTROLS;
    // Thiet bi: thu thuoc ve chinh cai may nay, dat mot lan roi hau nhu khong dong lai.
    case Action::BlePageTurner:
      return Tab::DEVICE;
    case Action::DeviceName:
    case Action::Network:
    case Action::Language:
      return Tab::DEVICE;
    case Action::KeyboardLayouts:
      return Tab::KEYBOARD;
    case Action::KOReaderSync:
    case Action::OPDSBrowser:
    case Action::FileTransfer:
    case Action::BrowseOPDS:
    case Action::ClearCache:
    case Action::CheckForUpdates:
    case Action::SdFirmwareUpdate:
      return Tab::OTHER;
    case Action::Clock:
      return Tab::SYSTEM;
    case Action::None:
      break;
  }
  return Tab::SYSTEM;
}

bool moTrinhChon(const int soLuaChon) { return soLuaChon >= 4; }

int dongCuaTheCaiDat(StrId* const out, const int max) {
  int n = 0;
  for (int i = 0; i < TAB_COUNT && n < max; i++) {
    out[n++] = tenThe(static_cast<Tab>(i));
  }
  return n;
}

}  // namespace settingstabs
