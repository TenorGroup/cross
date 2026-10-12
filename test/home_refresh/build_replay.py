"""Build the X4 Pro bus recorder against a selected SDK tree."""
import argparse
from pathlib import Path
import shutil
import subprocess

ap = argparse.ArgumentParser()
ap.add_argument('sdk', type=Path)
ap.add_argument('output', type=Path)
a = ap.parse_args()
lib = a.sdk.resolve() / 'libs/display/FreeInkDisplay'
root = a.output.resolve()
root.mkdir(parents=True, exist_ok=True)
for folder in ('src/driver', 'src/bus', 'src/lut', 'include'):
    (root / folder).mkdir(parents=True, exist_ok=True)
drivers = ('Ssd1677', 'Uc8179', 'Uc8279X4')
for name in drivers:
    for ext in ('cpp', 'h'):
        shutil.copy2(lib / f'src/driver/{name}Driver.{ext}', root / f'src/driver/{name}Driver.{ext}')
for path in ('src/driver/PanelDriver.h', 'src/FreeInkDisplay.cpp', 'include/FreeInkDisplay.h',
             'include/GrayscaleCapabilities.h', 'src/lut/Ssd1677Luts.h', 'src/lut/Uc8279X3Luts.h',
             'src/lut/UltraChipDirectGrayLuts.h'):
    shutil.copy2(lib / path, root / path)
for name in ('Arduino.h', 'BoardConfig.h', 'SPI.h', 'esp_heap_caps.h'):
    shutil.copy2(lib / 'test/host/pro_stubs' / name, root / name)
shutil.copy2(lib / 'test/host/pro_stubs/EpdBus.h', root / 'src/bus/EpdBus.h')
cmd = ['c++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Wno-unused-parameter', '-Wno-unused-function',
       '-DBOARD_HAS_PSRAM=1', '-DARDUINO=1', '-DEINK_DISPLAY_SINGLE_BUFFER_MODE=1',
       '-I' + str(root), '-I' + str(root / 'include'), str(Path(__file__).with_name('replay_x4.cpp')),
       str(root / 'src/FreeInkDisplay.cpp')]
cmd += [str(root / f'src/driver/{name}Driver.cpp') for name in drivers]
subprocess.run(cmd + ['-o', str(root / 'replay')], check=True)
