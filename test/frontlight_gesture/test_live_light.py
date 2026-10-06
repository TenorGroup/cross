"""Compile the actual ActivityManager::handleLightGesture with spy input and light.

Checks the light follows two fingers before they lift, a fast flick down puts it out keeping its level,
a slow stroke down only steps it, and the next stroke up brings the kept level back.
"""
from pathlib import Path
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[2]
source = (repo / 'src/activities/ActivityManager.cpp').read_text()
start = source.index('bool ActivityManager::applyLightLevel() {')
end = source.index('// The top menu: the light panel', start)
body = source[start:end]
fixture = r'''
#include <cassert>
#include <cstdint>
#include "''' + str(repo / 'src/components/FrontlightGesture.h') + r'''"
uint32_t clockMs = 0;
uint32_t millis() { return clockMs; }
#define LOG_INF(...) ((void)0)
namespace BoardConfig { bool isX4Pro() { return true; } }
struct Light {
  uint8_t level = 60, warm = 50; bool lit = true; int writes = 0;
  bool present() const { return true; }
  bool hasColorTemperature() const { return true; }
  uint8_t brightness() const { return level; }
  uint8_t warmth() const { return warm; }
  bool isOn() const { return lit; }
  void setBrightness(uint8_t v) { level = v; ++writes; }
  void setWarmth(uint8_t v) { warm = v; ++writes; }
  void setOn(bool on) { lit = on; ++writes; }
} Frontlight;
struct Settings { uint8_t frontlightBrightness = 60, frontlightWarmth = 50, frontlightOn = 1; } SETTINGS;
struct Input {
  uint8_t down = 0; int x = 0, y = 0;
  bool queued = false; uint8_t contacts = 2; int dx = 0, dy = 0; unsigned long ms = 0;
  bool touchContactsAt(uint8_t& count, int& cx, int& cy) const { count = down; cx = x; cy = y; return down > 0; }
  bool popMultiTouchSwipe(uint8_t& c, int& ddx, int& ddy, unsigned long* d) {
    if (!queued) return false;
    queued = false; c = contacts; ddx = dx; ddy = dy; *d = ms; return true;
  }
};
struct ActivityManager {
  Input mappedInput; FrontlightGesture lightGesture; int repaints = 0;
  void requestUpdate() { ++repaints; }
  bool handleLightGesture();
  bool applyLightLevel();
};
'''
test = r'''
int main() {
  ActivityManager m;
  auto at = [&](int y, uint32_t t) { m.mappedInput.down = 2; m.mappedInput.x = 240; m.mappedInput.y = y; clockMs = t; return m.handleLightGesture(); };
  // Fingers down and moving up: the light rises before any release.
  assert(at(500, 0) && Frontlight.level == 60);
  assert(at(470, 40) && Frontlight.level == 65 && Frontlight.lit && m.repaints == 1 && m.lightGesture.visible);
  assert(at(440, 80) && Frontlight.level == 70 && m.repaints == 2);
  // Lift with no flick: the steps stand, the release is consumed.
  m.mappedInput.down = 0; m.mappedInput.queued = true; m.mappedInput.dx = 0; m.mappedInput.dy = -60; m.mappedInput.ms = 300;
  assert(m.handleLightGesture() && Frontlight.level == 70 && Frontlight.lit);
  assert(!m.handleLightGesture());  // nothing left: screens get their input again
  // A slow stroke down (300 px in 1500 ms, 0.2 px/ms) only steps the light.
  assert(at(200, 2000));
  assert(at(260, 2600) && Frontlight.level == 60 && Frontlight.lit);
  m.mappedInput.down = 0; m.mappedInput.queued = true; m.mappedInput.dy = 300; m.mappedInput.ms = 1500;
  assert(m.handleLightGesture() && Frontlight.lit && Frontlight.level == 60);
  // A fast flick down (180 px in 150 ms, 1.2 px/ms): out at once, the level kept.
  assert(at(200, 4000));
  assert(at(320, 4080) && Frontlight.level == 40);
  m.mappedInput.down = 0; m.mappedInput.queued = true; m.mappedInput.dy = 180; m.mappedInput.ms = 150;
  assert(m.handleLightGesture() && !Frontlight.lit && Frontlight.level == 60);
  assert(SETTINGS.frontlightOn == 0 && SETTINGS.frontlightBrightness == 60 && m.lightGesture.dirty);
  // The next stroke up: the first step brings 60% back, the next adds 5%.
  assert(at(500, 6000));
  assert(at(470, 6050) && Frontlight.lit && Frontlight.level == 60);
  assert(at(440, 6100) && Frontlight.level == 65);
  // A controller that never showed the fingers live: the release steps the light by its whole travel.
  m.mappedInput.down = 0; m.mappedInput.queued = false; m.handleLightGesture();
  Frontlight.lit = false; Frontlight.level = 60; clockMs = 9000;
  m.mappedInput.queued = true; m.mappedInput.dx = 0; m.mappedInput.dy = -120; m.mappedInput.ms = 250;
  assert(m.handleLightGesture() && Frontlight.lit && Frontlight.level == 75);
  // Three fingers are not the light's.
  m.mappedInput.down = 0; m.mappedInput.queued = false; m.handleLightGesture();
  m.mappedInput.down = 3; assert(!m.handleLightGesture());
}
'''
with tempfile.TemporaryDirectory(prefix='live-light-') as tmp:
    cpp = Path(tmp) / 'live.cpp'
    exe = Path(tmp) / 'live'
    cpp.write_text(fixture + body + test)
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('GREEN: light follows two fingers live, a fast flick puts it out keeping its level, the next stroke brings it back')
