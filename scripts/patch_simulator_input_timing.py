"""Give the simulator the panel's waveform time and the board's input sampling.

Two hooks the P1 input-latency test needs, applied to the simulator dependency
only:

  CROSSPOINT_SIM_REFRESH_MS=<ms>
    HalDisplay::displayBuffer() sleeps that long before returning, standing in
    for the UC8279 waveform the firmware waits out with RenderLock still held
    (390 ms measured on the X3). 0/unset keeps the simulator instant.

  CROSSPOINT_SIM_STRICT_SAMPLING=1
    A press whose down and up edges both arrived since the last sample never
    existed on the board: the debounce needs two samples more than 5 ms apart,
    so a press that fits between two main-loop passes is dropped here too,
    instead of being replayed late. Each drop prints "[SIM] lost press
    button=<n>" on stderr.

Same contract as patch_simulator_grayscale.py: exactly one match per edit, skip
an edit already applied, fail loudly when the upstream text moved.
"""
from pathlib import Path

Import("env")
root = Path(env["PROJECT_LIBDEPS_DIR"]) / env["PIOENV"] / "simulator" / "src"


def patch(path, replacements):
    source = path.read_text()
    original = source
    for old, new in replacements:
        if new in source:
            continue
        if source.count(old) != 1:
            raise RuntimeError("Simulator input-timing source changed; review patch before building")
        source = source.replace(old, new, 1)
    if source != original:
        path.write_text(source)
        print("Applied simulator input-timing patch to " + path.name)


patch(
    root / "HalDisplay.cpp",
    [
        ("#include <array>\n#include <atomic>\n", "#include <array>\n#include <atomic>\n#include <chrono>\n"),
        (
            "void HalDisplay::displayBuffer(RefreshMode mode, bool turnOffScreen) {\n"
            "  refreshDisplay(mode, turnOffScreen);\n"
            "  if (std::this_thread::get_id() == simulatorMainThread) {\n"
            "    presentIfNeeded();\n"
            "  }\n"
            "}\n",
            "void HalDisplay::displayBuffer(RefreshMode mode, bool turnOffScreen) {\n"
            "  refreshDisplay(mode, turnOffScreen);\n"
            "  if (std::this_thread::get_id() == simulatorMainThread) {\n"
            "    presentIfNeeded();\n"
            "  }\n"
            "  // P1 test hook: stand in for the panel waveform the firmware waits out\n"
            "  // before returning, with the render lock still held.\n"
            "  const char *refreshMsValue = std::getenv(\"CROSSPOINT_SIM_REFRESH_MS\");\n"
            "  const long refreshMs =\n"
            "      refreshMsValue ? std::strtol(refreshMsValue, nullptr, 10) : 0;\n"
            "  if (refreshMs > 0)\n"
            "    std::this_thread::sleep_for(std::chrono::milliseconds(refreshMs));\n"
            "}\n",
        ),
    ],
)

patch(
    root / "HalGPIO.cpp",
    [
        ("#include <cstdio>\n", "#include <cstdio>\n#include <cstring>\n"),
        (
            "void processSyntheticEvents() {\n"
            "  initializeSyntheticEvents();\n"
            "  const unsigned long now = millis();\n"
            "  for (auto &event : syntheticEvents) {\n",
            "void processSyntheticEvents() {\n"
            "  initializeSyntheticEvents();\n"
            "  const unsigned long now = millis();\n"
            "  // P1 test hook: on the board the debounce needs two samples more than\n"
            "  // 5 ms apart, so a press whose down and up both landed since the last\n"
            "  // sample never happened. Drop such a pair, and say so.\n"
            "  const char *strictValue = std::getenv(\"CROSSPOINT_SIM_STRICT_SAMPLING\");\n"
            "  if (strictValue && std::strcmp(strictValue, \"1\") == 0) {\n"
            "    for (auto &down : syntheticEvents) {\n"
            "      if (down.handled || down.action != SyntheticAction::KeyDown ||\n"
            "          down.atMs > now)\n"
            "        continue;\n"
            "      for (auto &up : syntheticEvents) {\n"
            "        if (up.handled || up.action != SyntheticAction::KeyUp ||\n"
            "            up.button != down.button || up.atMs < down.atMs ||\n"
            "            up.atMs > now)\n"
            "          continue;\n"
            "        down.handled = true;\n"
            "        up.handled = true;\n"
            "        std::fprintf(stderr, \"[SIM] lost press button=%d\\n\", down.button);\n"
            "        break;\n"
            "      }\n"
            "    }\n"
            "  }\n"
            "  for (auto &event : syntheticEvents) {\n",
        ),
    ],
)

patch(
    root / "freertos" / "semphr.h",
    [
        (
            "// Returns pdTRUE (true) on success, pdFALSE (false) when ticksToWait expired.\n"
            "// Only the binary semaphore can fail: a mutex take waits as long as it must,\n"
            "// which is what every caller of the recursive mutex already assumes.\n",
            "// Returns pdTRUE (true) on success, pdFALSE (false) when ticksToWait expired.\n"
            "// A zero-tick take on a mutex is a try-lock, which is what the firmware's\n"
            "// RenderLock(TryTake{}) relies on; a longer take waits.\n",
        ),
        (
            "  auto *mtx = sim_semaphore_detail::asMutex(sem);\n" "  mtx->mtx.lock();\n",
            "  auto *mtx = sim_semaphore_detail::asMutex(sem);\n"
            "  // A zero-tick mutex take must fail instead of waiting: the firmware takes\n"
            "  // the render lock with a zero timeout on every loop pass, and a shim that\n"
            "  // waits there stalls the main loop for a whole frame and swallows input\n"
            "  // the board would have sampled.\n"
            "  if (ticksToWait == 0) {\n"
            "    if (!mtx->mtx.try_lock())\n"
            "      return false;\n"
            "  } else {\n"
            "    mtx->mtx.lock();\n"
            "  }\n",
        ),
    ],
)
