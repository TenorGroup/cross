#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

// The Settings row that names the panel chip, read only, for an owner reporting ink trouble
// (a photo of the Device tab says which controller, which VER bytes and which OEM record).
// Nothing here probes: the controller is the one the boot chose, VER comes from the boot probe
// or the wake memo (PanelMemo.h), and hw_calib/screenType is read from NVS, never written.
namespace panelchip {

// "UC8279, VER 00 00 02, NVS 2"; VER and NVS are left out when unknown (ver null, screenType < 0).
inline std::string text(const char* controller, const uint8_t* ver, const int screenType) {
  char out[48];
  int n = snprintf(out, sizeof(out), "%s", controller);
  if (ver && n < static_cast<int>(sizeof(out))) {
    n += snprintf(out + n, sizeof(out) - n, ", VER %02X %02X %02X", ver[0], ver[1], ver[2]);
  }
  if (screenType >= 0 && n < static_cast<int>(sizeof(out))) snprintf(out + n, sizeof(out) - n, ", NVS %d", screenType);
  return out;
}

// The row's value on this device, read once per boot.
const std::string& current();

}  // namespace panelchip
