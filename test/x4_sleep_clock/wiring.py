#!/usr/bin/env python3
"""Check the shared main sleep helper owns the clock-retention decision."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--source-root', type=Path, default=Path(__file__).resolve().parents[2])
parser.add_argument('--cxx', default='c++')
args = parser.parse_args()
source = (args.source_root / 'src/main.cpp').read_text()
start = source.index('static void sleepUntilPowerButton() {')
end = source.index('\nstatic bool autoSleepBlockedUntilInput', start)
helper = source[start:end]
# Compile the current helper verbatim. The rest of main.cpp and board hardware
# are outside this fixture; their integration is checked by target builds.
fixture = r'''
#include <cstdio>
#define LOG_INF(...) ((void)0)
struct GPIO { bool x4 = false; bool deviceIsX4() const { return x4; } } gpio;
struct Clock { bool valid = false; bool hasValidTime() const { return valid; } } halClock;
struct Power {
  unsigned calls = 0;
  bool retained = false;
  GPIO* received = nullptr;
  void startDeepSleep(GPIO& g, bool p = false) {
    ++calls; retained = p; received = &g;
  }
} powerManager;
''' + helper + r'''
int main() {
  unsigned scenarios = 0, failures = 0;
  for (bool x4 : {false, true}) for (bool valid : {false, true}) {
    gpio.x4 = x4; halClock.valid = valid;
    powerManager = Power{};
    sleepUntilPowerButton();
    ++scenarios;
    if (powerManager.calls != 1 || powerManager.received != &gpio || powerManager.retained != valid) {
      ++failures;
      std::printf("FAIL x4=%u valid=%u retained=%u\n", x4, valid, powerManager.retained);
    }
  }
  std::printf("main helper: %u scenarios, %u failures\n", scenarios, failures);
  return failures ? 1 : 0;
}
'''
fixture = '#include <initializer_list>\n' + fixture
with tempfile.TemporaryDirectory(prefix='x4-main-helper-') as temp:
    path = Path(temp)
    (path / 'helper.cpp').write_text(fixture)
    subprocess.run([args.cxx, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', str(path / 'helper.cpp'),
                    '-o', str(path / 'helper')], check=True)
    subprocess.run([str(path / 'helper')], check=True)
assert re.search(r'startDeepSleep\(gpio,\s*(?:halClock\.hasValidTime\(\)|preserveClock)\)', helper), \
    'Device sleep helper must pass current clock validity as the second argument'
assert 'SETTINGS' not in helper, 'Sleep wakes on the power button alone; no setting chooses wake buttons'
if 'preserveClock)' in helper:
    assert re.search(r'(?:const\s+)?bool\s+preserveClock\s*=\s*halClock\.hasValidTime\(\)', helper), \
        'Retention must use runtime validity, independently of display settings'
assert 'statusBarClock' not in helper and 'clockHasBeenSynced' not in helper, \
    'Clock visibility and historical settings do not establish runtime time validity'
outside = source[:start] + source[end:]
outside = re.sub(r'//[^\n]*', '', outside)
assert 'powerManager.startDeepSleep(' not in outside, 'Device sleep bypasses the shared helper'
assert len(re.findall(r'\bsleepUntilPowerButton\(\);', outside)) == 4, \
    'Review normal sleep, the two rejected wake routes (early, final), and USB boot sleep after topology changes'
print('PASS structural main sleep wiring: runtime epoch policy, four routes, no direct bypass')
