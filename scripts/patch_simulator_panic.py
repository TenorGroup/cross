"""Let a simulator test start the device as after a panic: CROSSPOINT_SIM_PANIC holds the reason it shows."""
from pathlib import Path

Import("env")
path = Path(env["PROJECT_LIBDEPS_DIR"]) / env["PIOENV"] / "simulator" / "src" / "HalSystem.cpp"
OLD = ('std::string HalSystem::getPanicInfo(bool full) { return {}; }\n'
       'bool HalSystem::isRebootFromPanic() { return false; }\n')
NEW = ('std::string HalSystem::getPanicInfo(bool) { const char* r = std::getenv("CROSSPOINT_SIM_PANIC"); return r ? r : ""; }\n'
       'bool HalSystem::isRebootFromPanic() { return std::getenv("CROSSPOINT_SIM_PANIC") != nullptr; }\n')
source = path.read_text()
if NEW not in source:
    if OLD not in source:
        raise RuntimeError("Simulator HalSystem changed; review the panic patch before building")
    path.write_text('#include <cstdlib>\n' + source.replace(OLD, NEW))
