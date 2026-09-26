#include "PanelChip.h"

#include <BoardConfig.h>
#include <HalDisplay.h>
#include <Logging.h>
#ifndef SIMULATOR
#include <HalGPIO.h>
#include <nvs.h>
#endif

namespace panelchip {
namespace {

const char* controllerName(const HalDisplay::Controller controller) {
  switch (controller) {
    case BoardConfig::DisplayController::SSD1677:
      return "SSD1677";
    case BoardConfig::DisplayController::UC8253:
      return "UC8253";
    case BoardConfig::DisplayController::UC8279:
      return "UC8279";
    case BoardConfig::DisplayController::UC8179:
      return "UC8179";
    default:
      return "?";
  }
}

// The OEM's per-unit record, -1 when the namespace or key is absent (or in the simulator).
int oemScreenType() {
#ifndef SIMULATOR
  nvs_handle_t handle;
  if (nvs_open("hw_calib", NVS_READONLY, &handle) != ESP_OK) return -1;
  uint8_t value = 0;
  const esp_err_t err = nvs_get_u8(handle, "screenType", &value);
  nvs_close(handle);
  return err == ESP_OK ? value : -1;
#else
  return -1;
#endif
}

}  // namespace

const std::string& current() {
  static const std::string value = [] {
    uint8_t ver[3] = {};
    bool haveVer = false;
#ifndef SIMULATOR
    haveVer = gpio.panelVersion(ver);
#endif
    std::string built = text(controllerName(display.getController()), haveVer ? ver : nullptr, oemScreenType());
    LOG_PROBE("SET", "Display chip: %s", built.c_str());
    return built;
  }();
  return value;
}

}  // namespace panelchip
