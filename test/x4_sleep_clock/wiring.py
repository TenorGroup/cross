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
start = source.index('static void sleepWithConfiguredButtons() {')
end = source.index('\nstatic bool autoSleepBlockedUntilInput', start)
helper = source[start:end]
# Compile the current helper verbatim. The rest of main.cpp and board hardware
# are outside this fixture; their integration is checked by target builds.
fixture = r'''
#include <cstdio>
#define LOG_INF(...) ((void)0)
struct GPIO { bool x4 = false; bool deviceIsX4() const { return x4; } } gpio;
struct Clock { bool valid = false; bool hasValidTime() const { return valid; } } halClock;
struct Settings { unsigned wakeButtons = 0; } SETTINGS;
struct Power {
  unsigned calls = 0, wake = 0;
  bool retained = false;
  GPIO* received = nullptr;
  void startDeepSleep(GPIO& g, unsigned w = 0, bool p = false) {
    ++calls; wake = w; retained = p; received = &g;
  }
} powerManager;
''' + helper + r'''
int main() {
  unsigned scenarios = 0, failures = 0;
  for (bool x4 : {false, true}) for (bool valid : {false, true}) for (unsigned wake : {0u, 3u}) {
    gpio.x4 = x4; halClock.valid = valid; SETTINGS.wakeButtons = wake;
    powerManager = Power{};
    sleepWithConfiguredButtons();
    ++scenarios;
    if (powerManager.calls != 1 || powerManager.received != &gpio ||
        powerManager.wake != wake || powerManager.retained != valid) {
      ++failures;
      std::printf("FAIL x4=%u valid=%u wake=%u retained=%u\n", x4, valid, wake, powerManager.retained);
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
assert re.search(r'startDeepSleep\(gpio,\s*SETTINGS\.wakeButtons,\s*(?:halClock\.hasValidTime\(\)|preserveClock)\)', helper), \
    'Device sleep helper must pass current clock validity as the third argument'
if 'preserveClock)' in helper:
    assert re.search(r'(?:const\s+)?bool\s+preserveClock\s*=\s*halClock\.hasValidTime\(\)', helper), \
        'Retention must use runtime validity, independently of display settings'
assert 'statusBarClock' not in helper and 'clockHasBeenSynced' not in helper, \
    'Clock visibility and historical settings do not establish runtime time validity'
outside = source[:start] + source[end:]
outside = re.sub(r'//[^\n]*', '', outside)
assert 'powerManager.startDeepSleep(' not in outside, 'Device sleep bypasses the shared helper'
assert len(re.findall(r'\bsleepWithConfiguredButtons\(\);', outside)) == 3, \
    'Review normal sleep, rejected wake, and USB boot sleep routes after topology changes'
print('PASS structural main sleep wiring: runtime epoch policy, three routes, no direct bypass')
