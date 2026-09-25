"""Simulator USB state from the environment.

A card runs on battery unless CROSSPOINT_SIM_USB=1. CROSSPOINT_SIM_USB_AT="<ms>:<0|1>;..." plugs the
cable in (1) or pulls it (0) at those milliseconds after start, so a test can change the state while
a screen is up. As on the device, the main loop's gpio.update() takes the state and reports the edge
through wasUsbStateChanged(); every other caller gets the state that pass took.
"""
from pathlib import Path

Import("env")
path = Path(env["PROJECT_LIBDEPS_DIR"]) / env["PIOENV"] / "simulator" / "src" / "HalGPIO.cpp"
UPSTREAM = "bool HalGPIO::isUsbConnected() const { return true; }\n"
# What the first version of this patch wrote; a libdeps tree built before it is rewritten.
ENV_ONLY = ("bool HalGPIO::isUsbConnected() const {\n"
            "  const char* usb = std::getenv(\"CROSSPOINT_SIM_USB\");\n"
            "  return usb != nullptr && usb[0] == '1';\n"
            "}\n")
HELPERS = ("namespace {\n"
           "bool simUsbScheduled() {\n"
           "  const char* usb = std::getenv(\"CROSSPOINT_SIM_USB\");\n"
           "  bool connected = usb != nullptr && usb[0] == '1';\n"
           "  const char* at = std::getenv(\"CROSSPOINT_SIM_USB_AT\");\n"
           "  const unsigned long now = millis();\n"
           "  while (at != nullptr && *at != '\\0') {\n"
           "    char* end = nullptr;\n"
           "    const unsigned long ms = std::strtoul(at, &end, 10);\n"
           "    if (end == at || *end != ':' || end[1] == '\\0') break;\n"
           "    if (now >= ms) connected = end[1] == '1';\n"
           "    at = std::strchr(end, ';');\n"
           "    if (at != nullptr) ++at;\n"
           "  }\n"
           "  return connected;\n"
           "}\n"
           "bool simUsbSeen = false;\n"
           "bool simUsbConnected = false;\n"
           "bool simUsbChanged = false;\n"
           "}  // namespace\n")
NEW = "bool HalGPIO::isUsbConnected() const { return simUsbSeen ? simUsbConnected : simUsbScheduled(); }\n"
OLD_EDGE = "bool HalGPIO::wasUsbStateChanged() const { return false; }\n"
NEW_EDGE = "bool HalGPIO::wasUsbStateChanged() const { return simUsbChanged; }\n"
UPDATE = "void HalGPIO::update() {\n"
UPDATE_USB = (HELPERS + "\n"
              "void HalGPIO::update() {\n"
              "  {\n"
              "    const bool connected = simUsbScheduled();\n"
              "    simUsbChanged = simUsbSeen && connected != simUsbConnected;\n"
              "    simUsbConnected = connected;\n"
              "    simUsbSeen = true;\n"
              "  }\n")

source = path.read_text()
if NEW not in source:
    old = ENV_ONLY if ENV_ONLY in source else UPSTREAM
    if source.count(old) != 1 or source.count(OLD_EDGE) != 1 or source.count(UPDATE) != 1:
        raise RuntimeError("Simulator USB source changed; review patch before building")
    source = source.replace(old, NEW).replace(OLD_EDGE, NEW_EDGE).replace(UPDATE, UPDATE_USB)
    if "#include <cstdlib>" not in source:
        source = "#include <cstdlib>\n" + source
    if "#include <cstring>" not in source:
        source = "#include <cstring>\n" + source
    path.write_text(source)
