#!/usr/bin/env python3
"""Compile the exact production transport namespace with fake WiFi boundaries.
TLS crypto is covered separately; this tests downloader -> HTTP -> wire integration.
"""
import argparse
from pathlib import Path
import subprocess
p = argparse.ArgumentParser()
p.add_argument('--source', type=Path)
p.add_argument('--fallback', action='store_true')
p.add_argument('--extract-only', action='store_true')
p.add_argument('--output', type=Path, required=True)
p.add_argument('--gtest', type=Path)
p.add_argument('--gtest-libs', type=Path)
a = p.parse_args()
repo = Path(__file__).resolve().parents[2]
a.output.mkdir(parents=True, exist_ok=True)
source = (a.source or repo / 'src/network/HttpDownloader.cpp').read_text()
start = source.index('namespace {')
end = source.index('}  // namespace', start) + len('}  // namespace')
(a.output / 'production_transport.inc').write_text(source[start:end])
if a.extract_only:
    raise SystemExit(0)
cmd = ['clang++', '-std=c++20', '-DSIMULATOR', '-Wno-character-conversion',
       '-I'+str(repo/'test/secure_http/stubs'), '-I'+str(repo/'freeink-sdk/libs/network/SecureNet/include'),
       '-I'+str(a.gtest/'include'), '-I'+str(a.output), str(repo/'test/http_downloader/transport_fixture.cpp'),
       str(a.gtest_libs/'libgtest.a'), str(a.gtest_libs/'libgtest_main.a'), '-o', str(a.output/'transport-test')]
if not a.fallback:
    cmd.insert(2, '-DFREEINK_NET_WOLFSSL=1')
subprocess.run(cmd, check=True)
raise SystemExit(subprocess.run([str(a.output/'transport-test')]).returncode)
