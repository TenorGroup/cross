"""Keep the pinned simulator clock facade aligned with the system-clock HAL."""
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
            raise RuntimeError("Simulator clock patch is only partially applied; review before building")
        if source.count(old) != count:
            raise RuntimeError("Simulator clock source changed; review patch before building")
        source = source.replace(old, new)
    if source != original:
        path.write_text(source)


patch(root / "HalClock.h", [(
    "  bool isAvailable() const { return _available; }\n",
    "  bool isAvailable() const { return _available; }\n  bool hasValidTime() const;\n",
    1,
)])

patch(root / "HalClock.cpp", [
    (
        "bool HalClock::getTime(uint8_t &hour, uint8_t &minute) const {\n",
        "bool HalClock::hasValidTime() const {\n"
        "  const std::time_t now = std::time(nullptr);\n"
        "  if (now < 1735689600) return false;\n"
        "  std::tm utc{};\n"
        "#if defined(_WIN32)\n"
        "  if (gmtime_s(&utc, &now) != 0) return false;\n"
        "#else\n"
        "  if (!gmtime_r(&now, &utc)) return false;\n"
        "#endif\n"
        "  return utc.tm_year <= 199;\n"
        "}\n\n"
        "bool HalClock::getTime(uint8_t &hour, uint8_t &minute) const {\n",
        1,
    ),
    ("  if (!_available)\n", "  if (!hasValidTime())\n", 2),
    ("  if (bufSize < 13u || !_available)\n", "  if (bufSize < 13u || !hasValidTime())\n", 1),
    (
        "bool HalClock::syncFromNTP() { return _available; }",
        "bool HalClock::syncFromNTP() { return hasValidTime(); }",
        1,
    ),
])
