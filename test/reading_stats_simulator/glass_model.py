"""The UC8279 glass model: build it, run the simulator with a panel trace, replay the trace.

glass/uc8279_glass.cpp compiles the SDK's UC8279 driver as it is, with its EpdBus fed into a model
of the controller RAM and the glass (the model's rules are at the top of that file). The simulator
writes the trace when CROSSPOINT_SIM_PANEL_TRACE names a file (scripts/patch_simulator_panel_trace.py).
"""

import json
import os
from pathlib import Path
import subprocess

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
SDK_DISPLAY = REPO / 'freeink-sdk/libs/display/FreeInkDisplay/src'
PROGRAM = Path(os.environ.get('TEST_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))
PANEL_BYTES = 528 * 792 // 8

ARDUINO = '''#pragma once
#include <cstddef>
#include <cstdint>
#define HIGH 1
#define LOW 0
#define PROGMEM
inline unsigned long millis() { static unsigned long t; return ++t; }
inline void delay(unsigned long) {}
inline int digitalRead(int) { return LOW; }
'''
SPI = '''#pragma once
struct SPISettings {
  SPISettings() {}
  SPISettings(unsigned long, unsigned char, unsigned char) {}
};
'''
BOARD = '''#pragma once
#include <cstdint>
namespace BoardConfig {
struct Active { uint16_t displayWidth = 792, displayHeight = 528; uint32_t displaySpiHz = 0; };
inline Active ACTIVE;
}
'''


def build(directory):
    """Compile the replay tool into `directory` and return its path."""
    directory = Path(directory)
    shim = directory / 'shim'
    shim.mkdir(parents=True, exist_ok=True)
    (shim / 'Arduino.h').write_text(ARDUINO)
    (shim / 'SPI.h').write_text(SPI)
    (shim / 'BoardConfig.h').write_text(BOARD)
    exe = directory / 'uc8279_glass'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-O2', '-Wall', '-Wextra', '-Wno-unused-parameter', '-Wno-unused-private-field',
                    '-I', str(shim), '-I', str(SDK_DISPLAY), str(HERE / 'glass/uc8279_glass.cpp'),
                    str(SDK_DISPLAY / 'driver/Uc8279Driver.cpp'), '-o', str(exe)], check=True)
    return exe


def run_simulator(sd, trace, script, after_wake=None, wake=None, extra=None, timeout=30):
    """One simulator process (and its deep-sleep wakes) writing its panel calls to `trace`."""
    env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
    env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_PANEL_TRACE=str(trace),
               CROSSPOINT_SIM_INPUT_SCRIPT=script)
    if after_wake:
        env['CROSSPOINT_SIM_INPUT_SCRIPT_AFTER_WAKE'] = after_wake
    if wake:
        env['CROSSPOINT_SIM_WAKE_REASON'] = wake
    env.update(extra or {})
    run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=timeout)
    return run.returncode, run.stdout + run.stderr


def replay(exe, trace, dumps=()):
    """Replay `trace`; one dict per panel call. `dumps` is [(record index, pgm path)]."""
    args = [str(exe), str(trace)]
    for index, path in dumps:
        args += ['--dump', f'{index}:{path}']
    out = subprocess.run(args, capture_output=True, text=True, check=True).stdout
    return [json.loads(line) for line in out.splitlines()]


def first_paint_after_last_begin(records):
    """The first B/W frame shown after the last process start: the wake's first screen."""
    start = max(r['i'] for r in records if r['op'] == 'begin')
    return next(r for r in records if r['i'] > start and r['op'] == 'display')
