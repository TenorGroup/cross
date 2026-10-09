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
        (
            "void HalDisplay::displayWindow(int, int, int, int) {\n"
            "  refreshDisplay(RefreshMode::FAST_REFRESH, false);\n"
            "}\n",
            "void HalDisplay::displayWindow(int x, int y, int width, int height) {\n"
            "  std::printf(\"WINDOW begin ms=%lu panel=%d,%d %dx%d\\n\", millis(), x, y, width, height);\n"
            "  refreshDisplay(RefreshMode::FAST_REFRESH, false);\n"
            "  const char *windowMsValue = std::getenv(\"CROSSPOINT_SIM_WINDOW_MS\");\n"
            "  const long windowMs = windowMsValue ? std::strtol(windowMsValue, nullptr, 10) : 0;\n"
            "  if (windowMs > 0) std::this_thread::sleep_for(std::chrono::milliseconds(windowMs));\n"
            "  std::printf(\"WINDOW end ms=%lu\\n\", millis());\n"
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

# A slow drag of 60 px or more (the flick's own distance) is a swipe whatever its duration, as in
# InputManager::wasSwipe. A dependency patched when the distance was 160 px moves to 60 first.
_gpio = root / "HalGPIO.cpp"
_was = _gpio.read_text()
_now = _was.replace("std::abs(dx) < 160 && std::abs(dy) < 160)", "std::abs(dx) < 60 && std::abs(dy) < 60)")
if _now != _was:
    _gpio.write_text(_now)
patch(
    root / "HalGPIO.cpp",
    [
        (
            "  if (!touchState.releasedThisFrame || touchState.suppressed ||\n"
            "      touchState.lastHeldMs > TOUCH_SWIPE_MAX_MS)\n"
            "    return false;\n"
            "  const float dx =\n"
            "      (touchState.currentNx - touchState.startNx) * HalDisplay::DISPLAY_WIDTH;\n"
            "  const float dy =\n"
            "      (touchState.currentNy - touchState.startNy) * HalDisplay::DISPLAY_HEIGHT;\n",
            "  if (!touchState.releasedThisFrame || touchState.suppressed)\n"
            "    return false;\n"
            "  const float dx =\n"
            "      (touchState.currentNx - touchState.startNx) * HalDisplay::DISPLAY_WIDTH;\n"
            "  const float dy =\n"
            "      (touchState.currentNy - touchState.startNy) * HalDisplay::DISPLAY_HEIGHT;\n"
            "  if (touchState.lastHeldMs > TOUCH_SWIPE_MAX_MS && std::abs(dx) < 60 && std::abs(dy) < 60)\n"
            "    return false;\n",
        ),
    ],
)

# A SWIPE in the input script moves the finger: one point 20 ms after the touch-down, at the far end, so a
# slow drag is a drag and not a hold (the real panel reports the finger all along).
patch(
    root / "HalGPIO.cpp",
    [
        ("  TouchDown,\n  TouchUp,\n", "  TouchDown,\n  TouchMove,\n  TouchUp,\n"),
        (
            "    case SyntheticAction::TouchUp:\n      endTouch(event.logicalNx, event.logicalNy);\n      break;\n",
            "    case SyntheticAction::TouchMove:\n      moveTouch(event.logicalNx, event.logicalNy);\n      break;\n"
            "    case SyntheticAction::TouchUp:\n      endTouch(event.logicalNx, event.logicalNy);\n      break;\n",
        ),
        (
            "          syntheticEvents.push_back(\n"
            "              {atMs + duration, SyntheticAction::TouchUp, -1, x2, y2});\n",
            "          if (swipe)\n"
            "            syntheticEvents.push_back({atMs + 20, SyntheticAction::TouchMove, -1, x2, y2});\n"
            "          syntheticEvents.push_back(\n"
            "              {atMs + duration, SyntheticAction::TouchUp, -1, x2, y2});\n",
        ),
    ],
)

# App-routing fixture for the SDK's already classified multi-contact queue.
# MULTISWIPE:count,x0,y0,x1,y1,ms contains logical coordinates; it emits no
# single-contact down/up. SDK classifier/GT911 acceptance is tested separately.
patch(root / 'HalGPIO.h', [(
    '  bool wasTouchActivity() const;\n',
    '  bool popMultiTouchSwipe(uint8_t &contacts, float &sx, float &sy, float &ex, float &ey,\n'
    '                          unsigned long &durationMs);\n'
    '  bool wasTouchActivity() const;\n',
), (
    '  bool popMultiTouchSwipe(uint8_t &contacts, float &sx, float &sy, float &ex, float &ey,\n',
    '  bool touchContactsAt(uint8_t &count, float &nx, float &ny) const;\n'
    '  bool popMultiTouchSwipe(uint8_t &contacts, float &sx, float &sy, float &ex, float &ey,\n',
)])
patch(root / 'HalGPIO.cpp', [(
    'std::vector<SyntheticEvent> syntheticEvents;\n',
    'struct MultiSwipeEvent {\n'
    '  unsigned long atMs, duration;\n'
    '  uint8_t contacts;\n'
    '  float sx, sy, ex, ey;\n'
    '  bool handled = false;\n'
    '};\n'
    'std::vector<MultiSwipeEvent> multiSwipeEvents;\n'
    'std::vector<SyntheticEvent> syntheticEvents;\n',
), (
    '      } else if ((key == "TAP" || key == "SWIPE") &&\n',
    '      } else if (key == "MULTISWIPE" && secondColon != std::string::npos) {\n'
    '        const std::string detail = item.substr(secondColon + 1);\n'
    '        const auto comma = detail.find(\',\');\n'
    '        const int contacts = std::atoi(detail.c_str());\n'
    '        float sx = 0, sy = 0, ex = 0, ey = 0;\n'
    '        unsigned long duration = 0;\n'
    '        if (contacts >= 1 && contacts <= 4 && comma != std::string::npos &&\n'
    '            parseTouchSpec(detail.substr(comma + 1), sx, sy, ex, ey, duration, true))\n'
    '          multiSwipeEvents.push_back({atMs + duration, duration, static_cast<uint8_t>(contacts), sx, sy, ex, ey});\n'
    '      } else if ((key == "TAP" || key == "SWIPE") &&\n',
), (
    'bool HalGPIO::wasSwipe(float &nxStart, float &nyStart, float &nxEnd,\n',
    'bool HalGPIO::popMultiTouchSwipe(uint8_t &contacts, float &sx, float &sy, float &ex, float &ey,\n'
    '                                unsigned long &durationMs) {\n'
    '  for (auto &event : multiSwipeEvents) {\n'
    '    if (event.handled || millis() < event.atMs) continue;\n'
    '    event.handled = true;\n'
    '    contacts = event.contacts;\n'
    '    durationMs = event.duration;\n'
    '    logicalToPanelNormalized(event.sx, event.sy, sx, sy);\n'
    '    logicalToPanelNormalized(event.ex, event.ey, ex, ey);\n'
    '    return true;\n'
    '  }\n'
    '  return false;\n'
    '}\n\n'
    'bool HalGPIO::wasSwipe(float &nxStart, float &nyStart, float &nxEnd,\n',
), (
    'bool HalGPIO::popMultiTouchSwipe(uint8_t &contacts, float &sx, float &sy, float &ex, float &ey,\n',
    '// The simulator has one pointer: never two contacts live.\n'
    'bool HalGPIO::touchContactsAt(uint8_t &count, float &, float &) const {\n'
    '  count = 0;\n'
    '  return false;\n'
    '}\n\n'
    'bool HalGPIO::popMultiTouchSwipe(uint8_t &contacts, float &sx, float &sy, float &ex, float &ey,\n',
)])

# The simulator's trimmed BoardConfig predates the warm-channel capability.
# Persist the same setting key as the X4 Pro SDK profile.
patch(root / 'BoardConfig.h', [(
    '#pragma once\n',
    '#pragma once\n\n#if defined(SIMULATOR_DEVICE_X4_PRO) && !defined(FREEINK_CAP_WARMLIGHT)\n'
    '#define FREEINK_CAP_WARMLIGHT 1\n#endif\n',
)])

# The simulator refreshes synchronously, so the panel is never busy after a refresh returns.
patch(root / 'HalDisplay.h', [(
    '  bool supportsAsyncRefresh() const;\n',
    '  bool supportsAsyncRefresh() const;\n'
    '  bool refreshBusy() { return false; }\n',
)])
