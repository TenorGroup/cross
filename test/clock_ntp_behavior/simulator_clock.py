#!/usr/bin/env python3
"""Compile the pinned simulator adapter before/after the reproducible patch."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

HARNESS = r'''
#include "HalClock.h"
#include <cstdio>
#include <ctime>
#include <string>
static std::time_t epoch = 0;
extern "C" std::time_t time(std::time_t* out) {
  if (out) *out = epoch;
  return epoch;
}
int main() {
  int failures = 0, checks = 0;
  auto check = [&](bool condition, const char* name) {
    ++checks;
    if (!condition) { ++failures; std::printf("FAIL %s\n", name); }
  };
  halClock.begin();
#ifdef SIMULATOR_DEVICE_X3
  check(halClock.isAvailable(), "X3 physical RTC");
#else
  check(!halClock.isAvailable(), "X4 physical RTC absent");
#endif
  uint16_t y = 0;
  uint8_t mo = 0, d = 0, h = 0, mi = 0;
  char text[32] = {};
  check(!halClock.getDateTime(y, mo, d, h, mi), "cold epoch has no date");
  check(!halClock.getTime(h, mi), "cold epoch has no time");
  check(!halClock.syncFromNTP(), "invalid host clock cannot sync");
  epoch = 1789819200;
  check(halClock.syncFromNTP(), "valid host clock sync");
  check(halClock.getDateTime(y, mo, d, h, mi), "date after sync");
  check(y == 2026 && mo == 9 && d == 19 && h == 12 && mi == 0, "correct UTC date");
  check(halClock.formatTime(text, sizeof(text), 76) && std::string(text) == "19:00", "UTC+7 display");
  check(halClock.formatDate(text, sizeof(text), 76), "date formatter available");
  epoch += 12 * 3600;
  check(halClock.getDateTime(y, mo, d, h, mi) && d == 20 && h == 0, "midnight");
  epoch = 4102444800LL;
  check(!halClock.getDateTime(y, mo, d, h, mi), "unsupported future date");
  check(!halClock.syncFromNTP(), "unsupported future sync");
  std::printf("%d clock assertions, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--simulator-src', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    repo = Path(__file__).resolve().parents[2]
    patch_text = (repo / 'scripts/patch_simulator_clock.py').read_text()
    target = output / 'libdeps' / 'probe' / 'simulator' / 'src'
    target.mkdir(parents=True)
    manifest = {'simulator_source': str(args.simulator_src.resolve()), 'source_hashes': {}, 'runs': []}
    for name in ('HalClock.cpp', 'HalClock.h'):
        original = args.simulator_src / name
        manifest['source_hashes'][name] = hashlib.sha256(original.read_bytes()).hexdigest()
        shutil.copyfile(original, target / name)
    (target / 'Arduino.h').write_text('#pragma once\n')
    (target / 'harness.cpp').write_text(HARNESS)

    def apply_patch():
        exec(compile(patch_text, 'patch_simulator_clock.py', 'exec'), {
            'Import': lambda *_: None,
            'env': {'PROJECT_LIBDEPS_DIR': str(output / 'libdeps'), 'PIOENV': 'probe'},
        })

    for phase in ('red', 'green'):
        if phase == 'green':
            apply_patch()
            first = [(target / n).read_bytes() for n in ('HalClock.cpp', 'HalClock.h')]
            apply_patch()
            assert first == [(target / n).read_bytes() for n in ('HalClock.cpp', 'HalClock.h')]
        for board in ('x4', 'x3'):
            binary = output / f'{phase}-{board}'
            cmd = ['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                   '-I' + str(target), str(target / 'HalClock.cpp'), str(target / 'harness.cpp'), '-o', str(binary)]
            if board == 'x3':
                cmd.append('-DSIMULATOR_DEVICE_X3')
            subprocess.run(cmd, check=True)
            run = subprocess.run([str(binary)], text=True, capture_output=True)
            (output / f'{phase}-{board}.log').write_text(run.stdout + run.stderr)
            manifest['runs'].append({'phase': phase, 'board': board, 'exit_code': run.returncode, 'command': cmd})
            print(phase, board, run.stdout.strip())
            assert (run.returncode != 0) if phase == 'red' else (run.returncode == 0)
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')


if __name__ == '__main__':
    main()
