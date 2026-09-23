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
p.add_argument('--reader-source', type=pathlib.Path)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
reader_source = a.reader_source or a.source
cpp = (reader_source / 'src/activities/reader/EpubReaderActivity.cpp').read_text()
header = (reader_source / 'src/activities/reader/EpubReaderActivity.h').read_text()

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
# Idle region of loop(): everything between the debounce constant and the background
# scheduler. The deferred cover thumbnail lives here, ahead of the page prewarm.
idle_start = loop.index('  constexpr unsigned long IDLE_PREWARM_DEBOUNCE_MS')
idle = 'void EpubReaderActivity::idleStep() {\n' + loop[idle_start:start] + '\n}'
# Cover-thumbnail tail of loadBook(), up to its final return.
load_book = function('loadBook')
thumb_start = load_book.index('  // The GAN DAY card only ever READS a thumbnail bitmap')
open_thumb = 'void EpubReaderActivity::openThumbStep() {\n' + load_book[thumb_start:load_book.rindex('  return true;')] + '\n}'
render = function('renderBook')
start = render.index('  if (section->isPartial() && section->currentPage >=')
end = render.index('  renderer.clearScreen();', start)
foreground = 'void EpubReaderActivity::foreground() {\n ReaderRenderSpec renderSpec; auto showBuildError=[]{ ++buildErrors; };\n' + render[start:end] + '\n}'
initial_branch = render.index('bool completedBuildTick = false;', render.index('const int target ='))
initial_end_marker = '\n          buildPopupPending = false;'
initial_end = render.index(initial_end_marker, initial_branch) + len(initial_end_marker)
initial_resume = (
    'void EpubReaderActivity::initialResume(int target) {\n'
    ' const bool anchorJump = false; const std::string pendingAnchor;\n'
    ' const std::optional<uint32_t> offsetJump; const unsigned long buildStartMs = millis();\n'
    ' buildPopupPending = true; ReaderRenderSpec renderSpec;\n'
    ' auto showBuildError=[]{ ++buildErrors; };\n'
    ' if (!section->startBuild(renderSpec)) { section.reset(); buildPopupPending = false; showBuildError(); return; }\n' +
    render[initial_branch:initial_end] + '\n}')
start = render.index('    auto p = section->loadPage(section->currentPage);')
end = render.index('    currentPageVisibleOffset = p->visibleTextOffset;', start)
page_load = ('void EpubReaderActivity::loadPageForRender() {\n'
             ' auto showPendingSyncSaveError=[]{};\n' + render[start:end] + '\n}')
# Constants and state declarations come from the real header, avoiding a second policy.
declarations = []
for line in header.splitlines():
    if re.match(r'  (?:static constexpr (?:size_t|int) (?:BACKGROUND_BUILD|BUILD_WINDOW|BUILD_PAGES|PARTIAL_REBUILD|RENDER_MIN_FREE_HEAP|THUMB_IDLE)|static constexpr unsigned long (?:BUILD_POPUP_DEADLINE_MS|RADIO_RELEASE_TIMEOUT_MS)|static constexpr uint8_t MAX_PAGE_LOAD_RETRIES|uint8_t (?:pageLoadRetryCount|pendingThumbCount)|size_t parkedParserFootprint|unsigned long lastRenderCompleteMs|int (?:idlePrewarmSpine|idlePrewarmPage|pendingThumbHeight)|bool (?:buildHeapPaused|backgroundBuildSuspended|backgroundBuildFailed|partialRebuildStartFailed|buildPopupPending|radioReleasedForBuild|pendingThumbGeneration)|uint16_t buildViewport)', line):
        declarations.append(line)
fixture = pathlib.Path(__file__).with_name('fixture.hpp').read_text().replace('@@FIELDS@@', '\n'.join(declarations))
functions = [function('buildTickHeapGate'), function('latTrangThat'), function('skipLoopDelay'), function('showBuildPopup')]
for name in ['deferBackgroundBuildForBle', 'backgroundBuildStartHeapGate', 'backgroundBuildCanTick', 'suspendBackgroundBuild', 'releaseRadioForBuild', 'showMemoryError', 'generatePendingThumb']:
    if 'EpubReaderActivity::' + name + '(' in cpp:
        functions.append(function(name))
reader = (a.source / 'src/activities/reader/ReaderActivity.cpp').read_text()
functions += [function(name, reader, 'ReaderActivity') for name in ['luotLatTrangNgoai', 'processExternalPageTurn', 'pageTurnLocked']]
assert loop.index('if (processExternalPageTurn()) return;') > loop.index('  if (handlePreviewInput()) return;')
cases = pathlib.Path(__file__).with_name('cases.cpp').read_text()
source = (fixture + '\n' + '\n'.join(functions) + '\n' + scheduler + '\n' + idle + '\n' + open_thumb + '\n' +
          foreground + '\n' + initial_resume + '\n' + page_load + '\n' + cases)
(a.output / 'projection.cpp').write_text(source)
hash_sources = {
    'src/activities/reader/EpubReaderActivity.cpp': reader_source,
    'src/activities/reader/EpubReaderActivity.h': reader_source,
    'src/activities/reader/ReaderActivity.cpp': a.source,
}
(a.output / 'source-hashes.json').write_text(json.dumps({str(path): hashlib.sha256((root / path).read_bytes()).hexdigest() for path, root in hash_sources.items()}, indent=2))
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
