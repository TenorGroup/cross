"""Keep the pinned simulator tilt facade aligned with the vertical gesture HAL."""
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
            raise RuntimeError("Simulator tilt patch is only partially applied; review before building")
        if source.count(old) != count:
            raise RuntimeError("Simulator tilt source changed; review patch before building")
        source = source.replace(old, new)
    if source != original:
        path.write_text(source)


patch(root / "HalTiltSensor.h", [(
    "  bool wasTiltedForward() { return false; }\n"
    "  bool wasTiltedBack() { return false; }\n",
    "  bool wasTiltedForward() { return false; }\n"
    "  bool wasTiltedBack() { return false; }\n"
    "  // Vertical menu gesture: the simulator has no gyro, so nothing ever fires.\n"
    "  void configureVerticalGesture(const uint8_t /*mode*/, const bool /*gestureTargetActive*/) {}\n"
    "  bool wasTiltedUp() { return false; }\n"
    "  bool wasTiltedDown() { return false; }\n",
    1,
)])
