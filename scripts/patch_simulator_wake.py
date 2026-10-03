"""Give the pinned simulator the three-call power-key hold check the firmware makes on a wake.

The simulator has no timer and no key to hold, so the hold always passes. Two switches stand in for
a short press, to run the wake's give-up paths:

  CROSSPOINT_SIM_WAKE_RELEASED=early   the key reads as let go at the early check
  CROSSPOINT_SIM_WAKE_RELEASED=final   it reads as held early and let go at the end

With either set, a QUIT that reaches the deep sleep ends the process there. The simulator would
otherwise return from the sleep and run the rest of the boot, which on the board never happens.

Same contract as patch_simulator_power.py: exactly one match per edit, skip an edit already
applied, fail loudly when the upstream text moved.
"""
from pathlib import Path

Import("env")
root = Path(env["PROJECT_LIBDEPS_DIR"]) / env["PIOENV"] / "simulator" / "src"


def patch(path, replacements):
    source = path.read_text()
    original = source
    for old, new, count in replacements:
        if source.count(new) == count:
            continue
        if new in source:
            raise RuntimeError("Simulator wake patch is only partially applied; review before building")
        if source.count(old) != count:
            raise RuntimeError("Simulator wake source changed; review patch before building")
        source = source.replace(old, new)
    if source != original:
        path.write_text(source)


HEADER = "  bool verifyPowerButtonWakeup();\n"
SOURCE = "bool HalGPIO::verifyPowerButtonWakeup() { return true; }\n"

patch(root / "HalGPIO.h", [(
    HEADER,
    HEADER +
    "  void beginPowerWakeHold();\n"
    "  bool powerWakeHeld();\n"
    "  bool endPowerWakeHold(bool waitFull);\n",
    1,
)])

patch(root / "HalGPIO.cpp", [(
    SOURCE,
    SOURCE +
    "namespace {\n"
    "bool simWakeReleased(const char *when) {\n"
    "  const char *value = std::getenv(\"CROSSPOINT_SIM_WAKE_RELEASED\");\n"
    "  return value && std::strcmp(value, when) == 0;\n"
    "}\n"
    "} // namespace\n"
    "void HalGPIO::beginPowerWakeHold() {}\n"
    "bool HalGPIO::powerWakeHeld() { return !simWakeReleased(\"early\"); }\n"
    "bool HalGPIO::endPowerWakeHold(bool /*waitFull*/) {\n"
    "  return !simWakeReleased(\"early\") && !simWakeReleased(\"final\");\n"
    "}\n",
    1,
)])

SLEEP_LOOP = (
    "    processSyntheticEvents();\n"
    "    if (quitRequested.load())\n"
    "      return;\n"
)
patch(root / "HalGPIO.cpp", [(
    SLEEP_LOOP,
    "    processSyntheticEvents();\n"
    "    if (quitRequested.load()) {\n"
    "      if (std::getenv(\"CROSSPOINT_SIM_WAKE_RELEASED\")) {\n"
    "        std::fflush(nullptr);\n"
    "        std::_Exit(0);\n"
    "      }\n"
    "      return;\n"
    "    }\n",
    1,
)])
