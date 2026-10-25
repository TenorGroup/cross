"""Compile the production HalGPIO::getWakeupReason (with isXteinkDevice and coldBootImpliesPowerButton).

A power-on reset with no USB is the power button only where the board powers on from the button alone (X3, X4).
On the X4 Classic and X4 Pro a no-USB power-on reset is the button only while the button is still held: a charger
that ends at 100% reads as no USB, and without the hold test every flash or cable change would ping-pong to sleep.
"""
from pathlib import Path
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[2]
source = (repo / 'lib/hal/HalGPIO.cpp').read_text()


def body(signature):
    start = source.index(signature)
    return source[start:source.index('\n}\n', start) + 3]


functions = ''.join(body(s) for s in ('bool HalGPIO::isXteinkDevice() const {',
                                      'bool HalGPIO::coldBootImpliesPowerButton() const {',
                                      'HalGPIO::WakeupReason HalGPIO::getWakeupReason() const {'))
fixture = r'''
#include <cassert>
#include <cstdio>
#include <initializer_list>
enum { ESP_RST_POWERON, ESP_RST_UNKNOWN, ESP_RST_DEEPSLEEP, ESP_RST_SW };
enum { ESP_SLEEP_WAKEUP_UNDEFINED, ESP_SLEEP_WAKEUP_GPIO, ESP_SLEEP_WAKEUP_EXT1, ESP_SLEEP_WAKEUP_TIMER };
int cause = ESP_SLEEP_WAKEUP_UNDEFINED, reset = ESP_RST_POWERON;
int esp_sleep_get_wakeup_cause() { return cause; }
int esp_reset_reason() { return reset; }
namespace BoardConfig {
enum class Board { XteinkX3, XteinkX3Uc8279, XteinkX4, XteinkX4Classic, XteinkX4Pro, Other };
struct Cfg { Board board; } ACTIVE{Board::XteinkX3};
inline bool isPaperMono() { return false; }
inline bool isSticky() { return false; }
inline bool isMetalioEInk4() { return false; }
inline bool isX4Classic() { return ACTIVE.board == Board::XteinkX4Classic; }
inline bool isX4Pro() { return ACTIVE.board == Board::XteinkX4Pro; }
}
struct Input { bool held = false; bool isPowerButtonPhysicallyPressed() const { return held; } };
struct HalGPIO {
  enum class WakeupReason { PowerButton, AfterFlash, AfterUSBPower, Other };
  Input inputMgr; bool usb = false;
  bool isUsbConnected() const { return usb; }
  bool isXteinkDevice() const;
  bool coldBootImpliesPowerButton() const;
  WakeupReason getWakeupReason() const;
};
'''
test = r'''
using R = HalGPIO::WakeupReason;
static R wake(BoardConfig::Board b, int rst, bool usb, bool held, int c = ESP_SLEEP_WAKEUP_UNDEFINED) {
  BoardConfig::ACTIVE.board = b; reset = rst; cause = c;
  HalGPIO g; g.usb = usb; g.inputMgr.held = held; return g.getWakeupReason();
}
int main() {
  using B = BoardConfig::Board;
  for (B b : {B::XteinkX4Classic, B::XteinkX4Pro}) {
    // The button still held after a power loss: the button woke it.
    assert(wake(b, ESP_RST_POWERON, false, true) == R::PowerButton);
    // Not held: a flash or a charger that ended at 100% (reads as no USB) must not sleep-loop.
    assert(wake(b, ESP_RST_POWERON, false, false) == R::Other);
    // USB in: the charge-sleep and flash paths are unchanged, held or not.
    assert(wake(b, ESP_RST_POWERON, true, true) == R::AfterUSBPower);
    assert(wake(b, ESP_RST_POWERON, true, false) == R::AfterUSBPower);
    assert(wake(b, ESP_RST_UNKNOWN, true, false) == R::AfterFlash);
    // A real deep sleep wake is the button either way.
    assert(wake(b, ESP_RST_DEEPSLEEP, false, false, ESP_SLEEP_WAKEUP_EXT1) == R::PowerButton);
  }
  // X3 and X4 power on from the button alone: a no-USB power-on reset is the button, held or not.
  for (B b : {B::XteinkX3, B::XteinkX3Uc8279, B::XteinkX4}) {
    assert(wake(b, ESP_RST_POWERON, false, false) == R::PowerButton);
    assert(wake(b, ESP_RST_POWERON, true, false) == R::AfterUSBPower);
  }
  std::puts("wake reason ok");
}
'''
with tempfile.TemporaryDirectory(prefix='wake-reason-') as tmp:
    cpp = Path(tmp) / 'wake.cpp'
    exe = Path(tmp) / 'wake'
    cpp.write_text(fixture + functions + test)
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-UNDEBUG', str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('GREEN: X4C and X4 Pro read a no-USB power-on reset as the button only while it is held')
