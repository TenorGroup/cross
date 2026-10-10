"""The X4 Pro simulator profile must permit real snapshot allocation."""
import argparse
import configparser
import subprocess
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--output', type=Path, required=True)
p.add_argument('--mutation', action='store_true')
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
a.output.mkdir(parents=True, exist_ok=True)
config = configparser.ConfigParser(interpolation=None, strict=False)
config.read(root / 'platformio.ini')
flags = config['env:simulator_x4pro']['build_flags'].split()
psram = '-DBOARD_HAS_PSRAM' in flags and not a.mutation
sim = root / '.pio/libdeps/simulator_x4pro/simulator/src'
path = a.output / 'snapshot.cpp'
path.write_text('''
#include "HalMemory.h"
#include "Arduino.h"
#include <cstdio>
ESPMock ESP;
int main() {
  for (const size_t size : {size_t(9464),size_t(22556),size_t(36720)}) {
    auto snapshot=HalMemory::allocatePsram(size);
    if(!snapshot) { std::printf("RED snapshot allocation bytes=%zu\\n",size); return 1; }
    snapshot[0]=0; snapshot[size-1]=255;
  }
  std::puts("GREEN 3 Home snapshot sizes allocated");
}
''')
exe = a.output / 'snapshot'
subprocess.run(['c++', '-std=c++17', *(['-DBOARD_HAS_PSRAM'] if psram else []),
                '-I'+str(sim), str(path), str(sim/'HalMemory.cpp'), '-o', str(exe)], check=True)
result = subprocess.run([str(exe)])
if a.mutation:
    assert result.returncode == 1, 'missing PSRAM profile mutation survived'
    print('GREEN: missing PSRAM profile mutation rejected')
else:
    result.check_returncode()
