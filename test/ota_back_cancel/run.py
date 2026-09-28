#!/usr/bin/env python3
"""Compile the production OTA install step, updater and Back sampler with a timed physical tap."""
import argparse
import json
from pathlib import Path
import re
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--output', type=Path, required=True)
p.add_argument('--cxx', default='c++')
a = p.parse_args()
here = Path(__file__).resolve().parent
repo = here.parents[1]
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=True)
activity = (repo / 'src/activities/settings/OtaUpdateActivity.cpp').read_text()
start = activity.index('void OtaUpdateActivity::runUpdateInstall() {')
end = activity.index('\nbool OtaUpdateActivity::idleExitDue(', start)
(out / 'production-install.inc').write_text(activity[start:end])
# The fake transport implements whichever streaming fetch the production header declares.
header = (repo / 'src/network/HttpDownloader.h').read_text()
streaming = re.search(r'static bool fetchUrl\(const std::string& url, const DataCallback& onData[^;]*;', header).group(0)
takes_cancel = 'cancelFlag' in streaming
binary = out / 'ota-back-cancel'
subprocess.run([a.cxx, '-std=c++17', '-pthread', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                '-fno-omit-frame-pointer', '-DCROSSPOINT_EMULATED=0', '-DFREEINK_DEVICE_X3=1',
                '-DCROSSPOINT_VERSION="1.0.16"', f'-DFETCH_TAKES_CANCEL={int(takes_cancel)}',
                '-I' + str(here / 'stubs'), '-I' + str(repo / 'test/file_transfer_back_latch/stubs'),
                '-I' + str(repo / 'src/util'), '-I' + str(repo / 'src/network'), '-I' + str(repo / 'lib/NetworkTrust'),
                '-I' + str(out), str(here / 'OtaCancel.cpp'), str(repo / 'src/network/OtaUpdater.cpp'),
                str(repo / 'src/network/FirmwareBoardTag.cpp'), str(repo / 'src/util/FileTransferBackLatch.cpp'),
                '-o', str(binary)], check=True)
cases = ('no-back-installs', 'back-mid-download', 'back-stalled-download', 'stall-ends-on-idle-deadline',
         'dry-run-keeps-boot-slot', 'older-refused-without-dry-run', 'dry-run-short-image-released',
         'manifest-outside-firmware-dir-refused')
results = []
for name in cases:
    run = subprocess.run([str(binary), name], capture_output=True, text=True)
    results.append({'case': name, 'exit': run.returncode, 'log': run.stdout + run.stderr})
    print(run.stdout + run.stderr, end='')
(out / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
print(f"{sum(r['exit'] == 0 for r in results)}/{len(cases)} passed")
raise SystemExit(int(any(r['exit'] for r in results)))
