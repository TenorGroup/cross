#!/usr/bin/env python3
"""Compile native HTTP adapter plus production downloader; exercise loopback faults."""
import argparse
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import time

p = argparse.ArgumentParser()
p.add_argument('--simulator-root', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
here = Path(__file__).resolve().parent
repo = here.parents[1]
a.output.mkdir(parents=True, exist_ok=True)
# Work only on a disposable copy of the three header-only native dependencies.
adapter = a.output / 'adapter'
(adapter / 'src').mkdir(parents=True, exist_ok=True)
for name in ('esp_http_client.h', 'esp_err.h', 'SimHttpFetch.h'):
    shutil.copy2(a.simulator_root / 'src' / name, adapter / 'src' / name)
patch = repo / 'scripts/patches/simulator-http-polling.patch'
if subprocess.run(['git', 'apply', '--reverse', '--check', str(patch)], cwd=adapter,
                  capture_output=True).returncode:
    subprocess.run(['git', 'apply', str(patch)], cwd=adapter, check=True)
source = (repo / 'src/network/HttpDownloader.cpp').read_text()
start = source.index('namespace {')
end = source.index('}  // namespace', start) + len('}  // namespace')
(a.output / 'production.inc').write_text(source[start:end])
binary = a.output / 'regression'
subprocess.run(['c++', '-std=c++17', '-DSIMULATOR', '-fsanitize=address,undefined',
                '-fno-omit-frame-pointer', '-I'+str(adapter/'src'), '-I'+str(a.output),
                str(here/'harness.cpp'), '-lcurl', '-o', str(binary)], check=True)
Path('/tmp/tenor-simulator-http-local.txt').write_text('LOCAL-FIXTURE')
Path('/tmp/tenor-simulator-http-mock').mkdir(exist_ok=True)
Path('/tmp/tenor-simulator-http-mock/local.txt').write_text('LOCAL-FIXTURE')
with socket.socket() as sock:
    sock.bind(('127.0.0.1', 0))
    port = sock.getsockname()[1]
with (a.output / 'fixture.log').open('w') as log:
    server = subprocess.Popen([sys.executable, str(here/'http_fixture.py'), '--host', '127.0.0.1',
                               '--port', str(port), '--hold-seconds', '3.2'], stdout=log, stderr=subprocess.STDOUT)
    try:
        for attempt in range(100):
            try:
                with socket.create_connection(('127.0.0.1', port), timeout=.1):
                    break
            except OSError:
                time.sleep(.01)
        result = subprocess.run([str(binary), f'http://127.0.0.1:{port}'], capture_output=True, text=True)
        output = result.stdout + result.stderr
        print(output, end='')
        (a.output/'result.log').write_text(output)
    finally:
        server.terminate()
        server.wait(timeout=5)
raise SystemExit(result.returncode)
