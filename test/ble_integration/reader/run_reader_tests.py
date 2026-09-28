#!/usr/bin/env python3
"""Compile real BLE/reader control flow with deterministic host boundaries."""
import argparse, pathlib, subprocess, json, hashlib, re
P = pathlib.Path
parser = argparse.ArgumentParser()
parser.add_argument('source')
parser.add_argument('output')
parser.add_argument('--compiler', default='c++')
args = parser.parse_args()
root = P(args.source)
out = P(args.output)
out.mkdir(parents=True, exist_ok=True)

def read(p):
    return (root / p).read_text()

def function(text, name):
    match = re.search('^\\s*(?:virtual\\s+|static\\s+)?(?:bool|void)\\s+' + re.escape(name) + '\\(', text, re.M)
    assert match, name
    pos = match.start()
    start = text.rfind('\n', 0, pos) + 1
    brace = text.index('{', pos)
    i = brace + 1
    depth = 1
    state = 'code'
    while depth:
        c = text[i]
        n = text[i + 1:i + 2]
        if state == 'line':
            if c == '\n':
                state = 'code'
        elif state == 'block':
            if c == '*' and n == '/':
                state = 'code'
                i += 1
        elif state in ('"', "'"):
            if c == '\\':
                i += 1
            elif c == state:
                state = 'code'
        elif c == '/' and n == '/':
            state = 'line'
            i += 1
        elif c == '/' and n == '*':
            state = 'block'
            i += 1
        elif c in ('"', "'"):
            state = c
        elif c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
        i += 1
    return text[start:i]
head = read('src/activities/reader/ReaderActivity.h')
body = read('src/activities/reader/ReaderActivity.cpp')

def reader_fn(name):
    if 'ReaderActivity::' + name + '(' in body:
        return function(body, 'ReaderActivity::' + name)
    f = function(head, name).strip()
    f = f.replace(name + '(', 'ReaderActivity::' + name + '(', 1)
    return f
new = 'processExternalPageTurn(' in body
layout_hook = 'pageAwaitsLayout(' in head
prefix = P(__file__).with_name('reader_fixture.hpp').read_text().replace('@@NEW@@', '1' if new else '0')
prefix = prefix.replace('@@LAYOUT@@', '1' if layout_hook else '0')
functions = [reader_fn(n) for n in ['pageTurn', 'luotLatTrangNgoai', 'handleEndOfBookPageTurn', 'endOfBookMenuActive', 'updateReadingTime', 'handlePreviewInput', 'loop']]
if 'luotNhayChuongNgoai(' in body:
    functions.append(reader_fn('luotNhayChuongNgoai'))
if new:
    for n in ['pageTurnLocked', 'processExternalPageTurn', 'cancelExternalPageTurn', 'queuePageTurn']:
        if n + '(' in body or n + '(' in head:
            functions.append(reader_fn(n))
for fmt in ['Txt', 'Xtc', 'Epub']:
    text = read('src/activities/reader/' + fmt + 'ReaderActivity.cpp')
    for n in ['latTrangThat', 'isAtEndOfBook', 'onReturnFromEndOfBook']:
        functions.append(function(text, fmt + 'ReaderActivity::' + n))
    if fmt == 'Epub':
        for n in ['externalPageTurnAllowed', 'manualPageTurnReady', 'pageAwaitsLayout']:
            if 'EpubReaderActivity::' + n + '(' in text:
                functions.append(function(text, 'EpubReaderActivity::' + n))
functions.append(function(read('src/activities/ActivityManager.cpp'), 'ActivityManager::pageTurn'))
manager = read('src/activities/ActivityManager.cpp')
if 'ActivityManager::chapterSkip(' in manager:
    functions.append(function(manager, 'ActivityManager::chapterSkip'))
epubloop = function(read('src/activities/reader/EpubReaderActivity.cpp'), 'EpubReaderActivity::loop')

def conditional_block(text, marker):
    pos = text.index(marker)
    brace = text.index('{', pos)
    depth = 1
    i = brace + 1
    while depth:
        if text[i] == '{':
            depth += 1
        if text[i] == '}':
            depth -= 1
        i += 1
    return (pos, text[pos:i])
selected = [conditional_block(epubloop, marker) for marker in ['if (overlay != Overlay::None)', 'if (handleEndOfBookMenu())', 'if (confirmReleased || ReaderUtils::isTouchMenuGesture', 'if (handleBackNavigation())']]
if 'if (processExternalPageTurn()) return;' in epubloop:
    selected.append((epubloop.index('if (processExternalPageTurn()) return;'), 'if (processExternalPageTurn()) return;'))
functions.append('void EpubReaderActivity::loop() {\n const bool confirmReleased = mappedInput.wasReleased(MappedInputManager::Button::Confirm);\n' + '\n'.join((part for (_, part) in sorted(selected))) + '\n}')
for n in ['externalPageTurnAllowed', 'manualPageTurnReady', 'pageAwaitsLayout']:
    if n + '(' in head:
        f = function(head, n).strip().replace('virtual ', '', 1).replace(n + '(', 'ReaderActivity::' + n + '(', 1)
        functions.append(f)
suffix = P(__file__).with_name('reader_cases.cpp').read_text()
main = read('src/main.cpp')
host = read('src/BlePageTurnerHost.cpp')
# The page turner pass of the main loop (the Scene the firmware builds, then bleturner::tick) and
# the host functions the module calls to deliver a press, as the firmware defines them.
tick_start = main.index('  bool bleInputActivity = false;\n  {\n    bleturner::Scene scene{};')
tick_end = main.index('    bleInputActivity = bleturner::tick(scene);\n  }\n', tick_start) + len('    bleInputActivity = bleturner::tick(scene);\n  }\n')
tick = main[tick_start:tick_end]
tilt_capture = re.search(r'  const bool pendingTiltActivity = halTiltSensor\.hadActivity\(\);\n', main)
assert tilt_capture
timers = main[main.index('  static unsigned long lastActivityTime = millis();'):]
end = re.search('  if \\([^\\n]*preventAutoSleep\\(\\)\\) lastSleepResetTime = millis\\(\\);', timers)
assert end
timers = timers[:end.end()]
timers = re.sub('  static unsigned long last(?:ActivityTime|SleepResetTime) = millis\\(\\);\\n', '', timers)
glue = function(main, 'runRemoteShortcut').replace('static void', 'void', 1) + '\n'
glue += 'void (*runShortcut)(bleturner::Action) = runRemoteShortcut;\n'
for name in ['bleDeliver', 'bleYieldForRadio', 'bleFileTransfer']:
    glue += function(host, name) + '\n'
glue += ('bleturner::Heap fixtureHeap(){return {100000,60000};}bool fixtureRelease(){return true;}void fixtureRestart(){}void fixtureHold(bool){}void fixtureLog(bool,const char*,va_list){}\n'
         'const bleturner::Host fixtureHost{fixtureHeap,fixtureRelease,bleDeliver,bleYieldForRadio,bleFileTransfer,fixtureRestart,fixtureHold,fixtureLog,nullptr};\n'
         'void resetModule(){freeink::ble::init=false;freeink::ble::startSuccess=true;freeink::ble::startQueued=false;freeink::ble::starts=0;bleturner::detail::resetForTests();'
         'SETTINGS.ble=bleturner::Config{};SETTINGS.ble.enabled=1;SETTINGS.ble.nextKeyUsage=1;SETTINGS.ble.prevKeyUsage=2;bleturner::begin(fixtureHost,SETTINGS.ble);}\n')
mainfixture = P(__file__).with_name('main_fixture.hpp').read_text() + '\n' + glue
pump = 'struct MainPump { unsigned long lastActivityTime=millis(),lastSleepResetTime=millis(); void pump(){\nfreeink::ble::runQueuedStart();\n' + tick + tilt_capture.group() + timers + '\nfreeink::ble::runQueuedStart();\n}};\n'
source = prefix + '\n\n' + '\n\n'.join(functions) + '\n\n' + mainfixture + pump + suffix
(out / 'reader_production.cpp').write_text(source)
files = ['lib/BlePageTurner/src/Runtime.cpp', 'lib/BlePageTurner/include/BlePageTurner.h', 'src/BlePageTurnerHost.cpp', 'src/main.cpp', 'src/activities/reader/ReaderActivity.h', 'src/activities/reader/ReaderActivity.cpp', 'src/activities/reader/EpubReaderActivity.cpp', 'src/activities/reader/TxtReaderActivity.cpp', 'src/activities/reader/XtcReaderActivity.cpp', 'src/activities/ActivityManager.cpp']
(out / 'source-hashes.json').write_text(json.dumps({p: hashlib.sha256((root / p).read_bytes()).hexdigest() for p in files}, indent=2) + '\n')
module = root / 'lib/BlePageTurner'
compile = subprocess.run([args.compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-fsanitize=address,undefined', '-g', '-DBLETURNER_TESTING', '-I' + str(module / 'include'), '-I' + str(module / 'src'), str(out / 'reader_production.cpp'), str(module / 'src/Runtime.cpp'), '-o', str(out / 'reader_production')], capture_output=True, text=True)
(out / 'compile.log').write_text(compile.stdout + compile.stderr)
if compile.returncode:
    print(compile.stderr)
    raise SystemExit(compile.returncode)
result = subprocess.run([str(out / 'reader_production')], capture_output=True, text=True)
(out / 'results.log').write_text(result.stdout + result.stderr)
print(result.stdout + result.stderr, end='')
raise SystemExit(result.returncode)
