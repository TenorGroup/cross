"""Give the pinned simulator power facade the battery display and gauge surface the firmware calls.

The simulator has no fuel gauge: its board profiles carry gauge address 0 (the ADC path, so the
About screen shows no gauge rows), the gauge diagnostics never become valid, and the shown battery
percentage is the raw one, since the simulated battery never moves.
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
            raise RuntimeError("Simulator power patch is only partially applied; review before building")
        if source.count(old) != count:
            raise RuntimeError("Simulator power source changed; review patch before building")
        source = source.replace(old, new)
    if source != original:
        path.write_text(source)


patch(root / "HalPowerManager.h", [(
    "  // Get battery percentage (range 0-100)\n"
    "  uint16_t getBatteryPercentage() const;\n",
    "  // Get battery percentage (range 0-100)\n"
    "  uint16_t getBatteryPercentage() const;\n"
    "  // The simulated battery never moves, so the shown value is the raw one.\n"
    "  uint16_t getDisplayedBatteryPercentage() const { return getBatteryPercentage(); }\n"
    "\n"
    "  // No fuel gauge in the simulator: the diagnostics stay invalid.\n"
    "  struct GaugeDiagnostics {\n"
    "    bool valid = false;\n"
    "    uint16_t millivolts = 0;\n"
    "    int16_t averageCurrentMa = 0;\n"
    "    uint16_t remainingCapacityMah = 0;\n"
    "    uint16_t fullChargeCapacityMah = 0;\n"
    "    uint16_t designCapacityMah = 0;\n"
    "    uint16_t stateOfChargePercent = 0;\n"
    "    uint16_t stateOfHealthPercent = 0;\n"
    "    uint16_t cycleCount = 0;\n"
    "    uint16_t statusFlags = 0;\n"
    "  };\n"
    "  const GaugeDiagnostics& gaugeDiagnostics() const {\n"
    "    static const GaugeDiagnostics none;\n"
    "    return none;\n"
    "  }\n",
    1,
)])

patch(root / "BoardConfig.h", [(
    "struct BoardProfile {\n",
    "// No fuel gauge in the simulator: address 0 selects the ADC path.\n"
    "struct BatteryGaugeConfig {\n"
    "  uint8_t gaugeAddr = 0;\n"
    "};\n"
    "\n"
    "struct BoardProfile {\n",
    1,
), (
    "  TouchConfig touch = {};\n"
    "};\n",
    "  TouchConfig touch = {};\n"
    "  BatteryGaugeConfig batteryGauge = {};\n"
    "};\n",
    1,
)])
