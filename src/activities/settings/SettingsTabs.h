#pragma once
#include <I18n.h>

#include <cstdint>

namespace settingstabs {

// Dong hanh dong: bam vao la MO MOT MAN KHAC, khong phai doi mot gia tri tai cho.
// Them muc moi thi THEM VAO CUOI.
enum class Action : uint8_t {
  None,
  RemapFrontButtons,
  CustomiseStatusBar,
  KOReaderSync,
  OPDSBrowser,
  Network,
  ClearCache,
  CheckForUpdates,
  SdFirmwareUpdate,
  Language,
  DownloadFonts,
  TextSettings,
  KeyboardLayouts,
  DeviceName,
  Clock,
  FileTransfer,
  BrowseOPDS,
};

enum class Tab : uint8_t { SCREEN, READER, CONTROLS, SYSTEM, DEVICE, KEYBOARD, OTHER };
inline constexpr int TAB_COUNT = 7;

// Nhan cua the.
StrId tenThe(Tab tab);

// Nha cua mot dong hanh dong. Moi dong co dung MOT nha; hai nha la mot dong hien hai
// lan, khong nha la mot dong bien mat im lang.
Tab nhaCua(Action action);

bool moTrinhChon(int soLuaChon);

int dongCuaTheCaiDat(StrId* out, int max);

}  // namespace settingstabs
