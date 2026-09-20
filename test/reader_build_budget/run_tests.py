#!/usr/bin/env python3
"""Compile reader lifecycle regions verbatim, with instrumented host boundaries."""
import argparse
import hashlib
import json
import pathlib
import re
import subprocess

p = argparse.ArgumentParser()
p.add_argument('source', type=pathlib.Path)
p.add_argument('output', type=pathlib.Path)
p.add_argument('--compiler', default='c++')
p.add_argument('--ble-capability', choices=('absent', '0', '1'), default='1')
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
cpp = (a.source / 'src/activities/reader/EpubReaderActivity.cpp').read_text()
header = (a.source / 'src/activities/reader/EpubReaderActivity.h').read_text()

def function(name, text=None, owner='EpubReaderActivity'):
    cpp = text if text is not None else globals()['cpp']
    start = cpp.index('bool ' + owner + '::' + name) if 'bool ' + owner + '::' + name in cpp else cpp.index('void ' + owner + '::' + name)
    brace = cpp.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (cpp[end] == '{') - (cpp[end] == '}')
        end += 1
    return cpp[start:end]

loop = function('loop')
start = loop.index('  // Background section builds') if '  // Background section builds' in loop else loop.index('  if (section && !section->isBuilding() && section->isPartial()')
end = loop.index('  if (handlePreviewInput()) return;', start)
scheduler = 'void EpubReaderActivity::backgroundTick() {\n' + loop[start:end] + '\n}'
render = function('renderBook')
start = render.index('  if (section->isPartial() && section->currentPage >=')
end = render.index('  renderer.clearScreen();', start)
foreground = 'void EpubReaderActivity::foreground() {\n ReaderRenderSpec renderSpec; auto showBuildError=[]{};\n' + render[start:end] + '\n}'
# Constants and state declarations come from the real header, avoiding a second policy.
declarations = []
for line in header.splitlines():
    if re.match(r'  (?:static constexpr (?:size_t|int) (?:BACKGROUND_BUILD|BUILD_WINDOW|BUILD_PAGES|PARTIAL_REBUILD)|bool (?:buildHeapPaused|backgroundBuildSuspended|partialRebuildStartFailed)|uint16_t buildViewport)', line):
        declarations.append(line)
fixture = pathlib.Path(__file__).with_name('fixture.hpp').read_text().replace('@@FIELDS@@', '\n'.join(declarations))
functions = [function('buildTickHeapGate'), function('latTrangThat'), function('skipLoopDelay')]
for name in ['deferBackgroundBuildForBle', 'backgroundBuildStartHeapGate', 'suspendBackgroundBuild']:
    if 'EpubReaderActivity::' + name + '(' in cpp:
        functions.append(function(name))
reader = (a.source / 'src/activities/reader/ReaderActivity.cpp').read_text()
functions += [function(name, reader, 'ReaderActivity') for name in ['luotLatTrangNgoai', 'processExternalPageTurn', 'pageTurnLocked']]
assert loop.index('if (processExternalPageTurn()) return;') > loop.index('  if (handlePreviewInput()) return;')
cases = pathlib.Path(__file__).with_name('cases.cpp').read_text()
source = fixture + '\n' + '\n'.join(functions) + '\n' + scheduler + '\n' + foreground + '\n' + cases
(a.output / 'projection.cpp').write_text(source)
(a.output / 'source-hashes.json').write_text(json.dumps({str(path): hashlib.sha256((a.source / path).read_bytes()).hexdigest() for path in ['src/activities/reader/EpubReaderActivity.cpp', 'src/activities/reader/EpubReaderActivity.h', 'src/activities/reader/ReaderActivity.cpp']}, indent=2))
cmd = [a.compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-fsanitize=address,undefined', '-g', str(a.output / 'projection.cpp'), '-o', str(a.output / 'projection')]
if a.ble_capability != 'absent':
    cmd.insert(1, '-DFREEINK_CAP_BLE_HID_HOST=' + a.ble_capability)
(a.output / 'compile-command.json').write_text(json.dumps(cmd, indent=2))
compiled = subprocess.run(cmd, capture_output=True, text=True)
(a.output / 'compile.log').write_text(compiled.stdout + compiled.stderr)
if compiled.returncode:
    print(compiled.stderr)
    raise SystemExit(compiled.returncode)
r = subprocess.run([str(a.output / 'projection')], capture_output=True, text=True)
(a.output / 'results.log').write_text(r.stdout + r.stderr)
print(r.stdout + r.stderr, end='')
raise SystemExit(r.returncode)
