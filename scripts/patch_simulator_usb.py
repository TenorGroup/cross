"""Simulator USB state from the environment: a card runs on battery unless CROSSPOINT_SIM_USB=1."""
from pathlib import Path

Import("env")
path = Path(env["PROJECT_LIBDEPS_DIR"]) / env["PIOENV"] / "simulator" / "src" / "HalGPIO.cpp"
OLD = "bool HalGPIO::isUsbConnected() const { return true; }\n"
NEW = ("bool HalGPIO::isUsbConnected() const {\n"
       "  const char* usb = std::getenv(\"CROSSPOINT_SIM_USB\");\n"
       "  return usb != nullptr && usb[0] == '1';\n"
       "}\n")
source = path.read_text()
if NEW not in source:
    if source.count(OLD) != 1:
        raise RuntimeError("Simulator USB source changed; review patch before building")
    source = source.replace(OLD, NEW)
    if "#include <cstdlib>" not in source:
        source = "#include <cstdlib>\n" + source
    path.write_text(source)
