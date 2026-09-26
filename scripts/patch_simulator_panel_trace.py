"""Let the simulator write down every panel call, for the UC8279 glass model.

  CROSSPOINT_SIM_PANEL_TRACE=<file>
    Every HalDisplay call that reaches the panel driver on the device is
    appended to <file>: the call, its arguments and the buffer it hands over
    (the framebuffer, a gray plane, strip rows). The file is opened for append
    and flushed after each record, so a deep-sleep wake (a fresh process) goes
    on writing the same trace. Unset writes nothing.

test/reading_stats_simulator/glass/ replays such a trace through the real
UC8279 driver of the SDK over a model of the controller RAM and the glass.

A call the simulator makes from inside another traced call (its grayscale base
showing the frame through displayBuffer) is part of that call and is not
written again. Record layout, little endian: 'T', op, a, b, c (u16), d (u16),
payload length (u32), payload.

Same contract as patch_simulator_grayscale.py: exactly one match per edit, skip
an edit already applied, fail loudly when the upstream text moved.
"""
from pathlib import Path

Import("env")
root = Path(env["PROJECT_LIBDEPS_DIR"]) / env["PIOENV"] / "simulator" / "src" / "HalDisplay.cpp"

TRACE = '''// Panel trace for the UC8279 glass model (scripts/patch_simulator_panel_trace.py).
namespace simtrace {
enum Op : uint8_t {
  BEGIN = 1, DISPLAY, GRAY_BASE, GRAY_BASE_MODE, COPY_LSB, COPY_MSB, STRIP,
  GRAY_DISPLAY, CLEANUP, PRECONDITION, DEEP_SLEEP, SET_INVERTED
};
thread_local int depth = 0;
struct Scope {
  const bool outer;
  Scope() : outer(depth++ == 0) {}
  ~Scope() { --depth; }
};
void put(uint8_t op, uint8_t a, uint8_t b, uint16_t c, uint16_t d,
         const uint8_t *data, uint32_t len) {
  static std::mutex lock;
  static FILE *file = [] {
    const char *path = std::getenv("CROSSPOINT_SIM_PANEL_TRACE");
    return path && path[0] ? std::fopen(path, "ab") : nullptr;
  }();
  if (!file)
    return;
  const std::lock_guard<std::mutex> guard(lock);
  if (!data)
    len = 0;
  const uint8_t head[12] = {'T', op, a, b,
                            static_cast<uint8_t>(c), static_cast<uint8_t>(c >> 8),
                            static_cast<uint8_t>(d), static_cast<uint8_t>(d >> 8),
                            static_cast<uint8_t>(len), static_cast<uint8_t>(len >> 8),
                            static_cast<uint8_t>(len >> 16), static_cast<uint8_t>(len >> 24)};
  std::fwrite(head, 1, sizeof(head), file);
  if (len)
    std::fwrite(data, 1, len, file);
  std::fflush(file);
}
} // namespace simtrace

static SDL_Window *window = nullptr;'''

FB = "getFrameBuffer(), BUFFER_SIZE"

edits = [
    ("static SDL_Window *window = nullptr;", TRACE),
    (
        "void HalDisplay::begin(bool /*seamless*/) { begin(); }",
        "void HalDisplay::begin(bool seamless) {\n"
        "  simtrace::put(simtrace::BEGIN, seamless, 0, 0, 0, nullptr, 0);\n"
        "  begin();\n"
        "}",
    ),
    (
        "void HalDisplay::setInverted(bool value) { inverted = value; }",
        "void HalDisplay::setInverted(bool value) {\n"
        "  simtrace::put(simtrace::SET_INVERTED, value, 0, 0, 0, nullptr, 0);\n"
        "  inverted = value;\n"
        "}",
    ),
    (
        "bool HalDisplay::toggleInverted() {\n  inverted = !inverted;\n",
        "bool HalDisplay::toggleInverted() {\n  inverted = !inverted;\n"
        "  simtrace::put(simtrace::SET_INVERTED, inverted, 0, 0, 0, nullptr, 0);\n",
    ),
    (
        "void HalDisplay::refreshDisplay(RefreshMode /*mode*/, bool /*turnOffScreen*/) {\n",
        "void HalDisplay::refreshDisplay(RefreshMode mode, bool turnOffScreen) {\n"
        "  const simtrace::Scope trace;\n"
        "  if (trace.outer)\n"
        f"    simtrace::put(simtrace::DISPLAY, mode, turnOffScreen, 0, 0, {FB});\n",
    ),
    (
        "void HalDisplay::deepSleep() { presentIfNeeded(); }",
        "void HalDisplay::deepSleep() {\n"
        "  simtrace::put(simtrace::DEEP_SLEEP, 0, 0, 0, 0, nullptr, 0);\n"
        "  presentIfNeeded();\n"
        "}",
    ),
    (
        "void HalDisplay::copyGrayscaleBuffers(const uint8_t *lsbBuffer,\n"
        "                                      const uint8_t *msbBuffer) {\n",
        "void HalDisplay::copyGrayscaleBuffers(const uint8_t *lsbBuffer,\n"
        "                                      const uint8_t *msbBuffer) {\n"
        "  const simtrace::Scope trace;\n"
        "  if (trace.outer) {\n"
        "    simtrace::put(simtrace::COPY_LSB, 0, 0, 0, 0, lsbBuffer, BUFFER_SIZE);\n"
        "    simtrace::put(simtrace::COPY_MSB, 0, 0, 0, 0, msbBuffer, BUFFER_SIZE);\n"
        "  }\n",
    ),
    (
        "void HalDisplay::displayGrayscaleBase(RefreshMode fallback,\n"
        "                                      bool turnOffScreen) {\n",
        "void HalDisplay::displayGrayscaleBase(RefreshMode fallback,\n"
        "                                      bool turnOffScreen) {\n"
        "  const simtrace::Scope trace;\n"
        "  if (trace.outer)\n"
        f"    simtrace::put(simtrace::GRAY_BASE, fallback, turnOffScreen, 0, 0, {FB});\n",
    ),
    (
        "bool HalDisplay::displayGrayscaleBase(GrayscaleMode mode,\n"
        "                                      RefreshMode fallback,\n"
        "                                      bool turnOffScreen) {\n",
        "bool HalDisplay::displayGrayscaleBase(GrayscaleMode mode,\n"
        "                                      RefreshMode fallback,\n"
        "                                      bool turnOffScreen) {\n"
        "  const simtrace::Scope trace;\n"
        "  if (trace.outer)\n"
        "    simtrace::put(simtrace::GRAY_BASE_MODE, static_cast<uint8_t>(mode), fallback, turnOffScreen, 0,\n"
        f"                  {FB});\n",
    ),
    (
        "void HalDisplay::preconditionGrayscale() {}",
        "void HalDisplay::preconditionGrayscale() {\n"
        "  simtrace::put(simtrace::PRECONDITION, 1, 0, 0, 0, nullptr, 0);\n"
        "}",
    ),
    (
        "void HalDisplay::preconditionGrayscale(uint16_t, uint16_t, uint16_t, uint16_t) {\n}",
        "void HalDisplay::preconditionGrayscale(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {\n"
        "  const uint8_t rect[4] = {static_cast<uint8_t>(y), static_cast<uint8_t>(y >> 8),\n"
        "                           static_cast<uint8_t>(h), static_cast<uint8_t>(h >> 8)};\n"
        "  simtrace::put(simtrace::PRECONDITION, 0, 0, x, w, rect, sizeof(rect));\n"
        "}",
    ),
    (
        "void HalDisplay::copyGrayscaleLsbBuffers(const uint8_t *lsbBuffer) {\n",
        "void HalDisplay::copyGrayscaleLsbBuffers(const uint8_t *lsbBuffer) {\n"
        "  const simtrace::Scope trace;\n"
        "  if (trace.outer)\n"
        "    simtrace::put(simtrace::COPY_LSB, 0, 0, 0, 0, lsbBuffer, BUFFER_SIZE);\n",
    ),
    (
        "void HalDisplay::copyGrayscaleMsbBuffers(const uint8_t *msbBuffer) {\n",
        "void HalDisplay::copyGrayscaleMsbBuffers(const uint8_t *msbBuffer) {\n"
        "  const simtrace::Scope trace;\n"
        "  if (trace.outer)\n"
        "    simtrace::put(simtrace::COPY_MSB, 0, 0, 0, 0, msbBuffer, BUFFER_SIZE);\n",
    ),
    (
        "void HalDisplay::cleanupGrayscaleBuffers(const uint8_t *bwBuffer) {\n"
        "  grayscalePreviewState.absolute = false;\n",
        "void HalDisplay::cleanupGrayscaleBuffers(const uint8_t *bwBuffer) {\n"
        "  grayscalePreviewState.absolute = false;\n"
        "  simtrace::put(simtrace::CLEANUP, 0, 0, 0, 0, bwBuffer, BUFFER_SIZE);\n",
    ),
    (
        "void HalDisplay::displayGrayBuffer(bool, const unsigned char *, bool) {\n",
        "void HalDisplay::displayGrayBuffer(bool turnOffScreen, const unsigned char *lut,\n"
        "                                   bool factoryMode) {\n"
        "  simtrace::put(simtrace::GRAY_DISPLAY, turnOffScreen, lut != nullptr, factoryMode, 0,\n"
        f"                  {FB});\n",
    ),
    (
        "void HalDisplay::writeGrayscalePlaneStrip(bool lsbPlane, const uint8_t *rows,\n"
        "                                          uint16_t yStart, uint16_t numRows) {\n",
        "void HalDisplay::writeGrayscalePlaneStrip(bool lsbPlane, const uint8_t *rows,\n"
        "                                          uint16_t yStart, uint16_t numRows) {\n"
        "  simtrace::put(simtrace::STRIP, lsbPlane, 0, yStart, numRows, rows,\n"
        "                  static_cast<uint32_t>(numRows) * DISPLAY_WIDTH_BYTES);\n",
    ),
]

if root.exists():
    source = root.read_text()
    original = source
    for old, new in edits:
        if new in source:
            continue
        if source.count(old) != 1:
            raise RuntimeError("Simulator panel-trace source changed; review patch before building")
        source = source.replace(old, new, 1)
    if source != original:
        root.write_text(source)
        print("Applied simulator panel-trace patch to " + root.name)
