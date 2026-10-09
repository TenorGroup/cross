"""Keep the pinned simulator tilt facade aligned with the vertical gesture and shake HAL."""
from pathlib import Path
import subprocess
import sys

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
), (
    "  bool hadActivity() { return false; }\n",
    "  // Flick strength per axis: nothing to tune without a gyro.\n"
    "  void setStrength(const uint8_t /*horizontal*/, const uint8_t /*vertical*/) {}\n"
    "  bool hadActivity() { return false; }\n",
    1,
), (
    "  void clearPendingEvents() {}\n",
    "  // Hard shake: no accelerometer either, so no shake ever comes.\n"
    "  void configureShake(const uint8_t /*action*/, const uint8_t /*strength*/) {}\n"
    "  bool wasShaken() { return false; }\n"
    "  void clearPendingEvents() {}\n",
    1,
), (
    "  // Hard shake: no accelerometer either, so no shake ever comes.\n",
    "  // Side flicks waiting for the hand to come back: none without a gyro.\n"
    "  void confirmSideFlicks(const bool /*confirm*/) {}\n"
    "  // Hard shake: no accelerometer either, so no shake ever comes.\n",
    1,
), (
    "  void clearPendingEvents() {}\n",
    "  void clearPendingEvents() {}\n"
    "  // Face down and face up: no accelerometer, so the device never turns over.\n"
    "  void configureFlip(const uint8_t /*faceDownAction*/, const uint8_t /*faceUpAction*/) {}\n"
    "  bool wasTurnedFaceDown() { return false; }\n"
    "  bool wasTurnedFaceUp() { return false; }\n",
    1,
), (
    "  bool wasTurnedFaceUp() { return false; }\n",
    "  bool wasTurnedFaceUp() { return false; }\n"
    "  // Double tap: no accelerometer, so no tap ever comes.\n"
    "  void configureDoubleTap(const uint8_t /*action*/) {}\n"
    "  bool wasDoubleTapped() { return false; }\n",
    1,
), (
    "  bool wasDoubleTapped() { return false; }\n",
    "  bool wasDoubleTapped() { return false; }\n"
    "  // The screen in front: no flick ever waits for the hand without a gyro.\n"
    "  void noteScreen(const uint32_t /*screen*/) {}\n",
    1,
), (
    "  void noteScreen(const uint32_t /*screen*/) {}\n",
    "  void noteScreen(const uint32_t /*screen*/) {}\n"
    "  // Double tap on the back, the screen and a side edge: none without an accelerometer.\n"
    "  void configureDoubleTap(const uint8_t /*back*/, const uint8_t /*screen*/, const uint8_t /*edge*/) {}\n"
    "  bool wasScreenTapped() { return false; }\n"
    "  bool wasEdgeTapped() { return false; }\n",
    1,
)])

subprocess.run([sys.executable,
                str(Path(env['PROJECT_DIR']) / 'test/reading_stats_simulator/ugly_tilt_fixture.py'),
                '--hal', str(root / 'HalTiltSensor.h')], check=True)
