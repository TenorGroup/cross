#pragma once
#include <I18n.h>

#include <array>
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
  ClockSettings,
  FileTransfer,
  BrowseOPDS,
  BlePageTurner,
  HomeButton,
  About,
  Plugins,
  OpenPlugins,
};

// MOTION is last by ID: a board without a motion sensor shows the tabs below it only.
enum class Tab : uint8_t { SCREEN, READER, CONTROLS, SYSTEM, DEVICE, KEYBOARD, OTHER, SLEEP, MOTION };
inline constexpr int TAB_COUNT = 9;
// Display order is independent of stable persisted tab IDs. One order on every device, X4 Pro's (founder
// 20/10/2026): Display, Reader, Controls | System, Device, Keyboard, Motion | Sleep, Other.
inline constexpr std::array<uint8_t, TAB_COUNT> DEFAULT_ORDER = {0, 1, 2, 3, 4, 5, 8, 7, 6};
// The default orders up to v1.0.68 (9 tabs, and 8 before Motion, its ID appended on load). Every pin saved the
// whole order, so a file holding one of these never chose it: it loads as DEFAULT_ORDER.
inline constexpr std::array<uint8_t, TAB_COUNT> OLD_DEFAULT_ORDERS[] = {{0, 7, 1, 2, 8, 3, 4, 5, 6},
                                                                       {0, 7, 1, 2, 3, 4, 5, 6, 8}};

// Nhan cua the.
StrId tenThe(Tab tab);

// Nha cua mot dong hanh dong. Moi dong co dung MOT nha; hai nha la mot dong hien hai
// lan, khong nha la mot dong bien mat im lang.
Tab nhaCua(Action action);

bool moTrinhChon(int soLuaChon);

int dongCuaTheCaiDat(StrId* out, int max);

}  // namespace settingstabs
