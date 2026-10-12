#!/usr/bin/env python3
"""Project production scheduler, electrical input and BLE routes into one host loop."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('source', type=Path)
parser.add_argument('output', type=Path)
parser.add_argument('--reader-source', type=Path)
parser.add_argument('--compiler', default='c++')
parser.add_argument('--prepare-only', action='store_true')
a = parser.parse_args()
root, out = a.source.resolve(), a.output.resolve()
reader_root = (a.reader_source or root).resolve()
out.mkdir(parents=True, exist_ok=True)
files = {}
def read(path, source=root):
    text = (source / path).read_text()
    files[str(source / path)] = hashlib.sha256(text.encode()).hexdigest()
    return text

def function(text, name):
    match = re.search(r'^\s*(?:static\s+)?(?:bool|void|int|uint8_t)\s+' + re.escape(name) + r'\(', text, re.M)
    assert match, name
    start = match.start()
    brace = text.index('{', start)
    depth, i = 1, brace + 1
    while depth:
        depth += (text[i] == '{') - (text[i] == '}')
        i += 1
    return text[start:i]

cpp = read('src/activities/reader/EpubReaderActivity.cpp', reader_root)
header = read('src/activities/reader/EpubReaderActivity.h', reader_root)
loop = function(cpp, 'EpubReaderActivity::loop')
start = loop.index('  // Background section builds')
end = loop.index('  if (handlePreviewInput()) return;', start)
edge = loop.index('  const bool inputThisPass')
scheduler = 'void EpubReaderActivity::backgroundTick() {\n' + loop[edge:loop.index(';', edge)+1] + '\n' + loop[start:end] + '\n}'
fixture = read('test/reader_build_budget/fixture.hpp')
if 'struct HalPowerManager' not in fixture:
    fixture = 'struct HalPowerManager { struct Lock {}; };\n' + fixture
fields = []
for line in header.splitlines():
    if re.match(r'  (?:static constexpr (?:size_t|int) (?:BACKGROUND_BUILD|BUILD_WINDOW|BUILD_PAGES|PARTIAL_REBUILD|RENDER_MIN_FREE_HEAP|THUMB_IDLE|LOOK_AHEAD_PAGES)|static constexpr unsigned long (?:BUILD_POPUP_DEADLINE_MS|RADIO_RELEASE_TIMEOUT_MS)|uint8_t (?:pageLoadRetryCount|pendingThumbCount)|size_t parkedParserFootprint|unsigned long (?:lastRenderCompleteMs|radioSettledMs|lookAhead)|int (?:idlePrewarmSpine|idlePrewarmPage|pendingThumbHeight|lookAhead)|bool (?:buildHeapPaused|backgroundBuildSuspended|backgroundBuildFailed|partialRebuildStartFailed|buildPopupPending|buildPopupRefreshing|radioReleasedForBuild|fontsShedForBuild|pendingThumbGeneration|coverRefPending)|uint16_t buildViewport)', line):
        fields.append(line)
fixture = fixture.replace('namespace StrId { constexpr int', 'namespace StrId { [[maybe_unused]] constexpr int')
fixture = fixture.replace('@@FIELDS@@', '\n'.join(fields)).replace('@@LAYOUT@@', '  bool pageAwaitsLayout() const;')
fixture = re.sub(r'uint32_t clockMs = 1000;\nuint32_t millis\(\) \{ return clockMs; \}', '', fixture)
start = fixture.index('namespace bleturner {')
end = fixture.index('}  // namespace bleturner', start) + len('}  // namespace bleturner')
fixture = fixture[:start] + fixture[end:]
start = fixture.index('struct Gpio {') if 'struct Gpio {' in fixture else fixture.index('struct GPIO {')
end = fixture.index('} gpio;', start) + len('} gpio;')
fixture = fixture[:start] + fixture[end:]
fixture = fixture.replace('struct Manager {', '''struct ReaderActivity;
struct Manager {
  struct Current { ReaderActivity* value=nullptr; ReaderActivity* get() const { return value; } } currentActivity;
  bool sleepTransition=false;
  bool isForegroundReaderReady() const { return currentActivity.get() != nullptr; }
  bool pageTurn(bool);
  bool chapterSkip(bool) { throw std::runtime_error("unexpected chapter turn"); }''')
fixture = re.sub(r'  struct Input \{.*?\} mappedInput;', '  struct Input { bool wasAnyPressed() const { return gpio.wasAnyPressed(); } bool wasAnyReleased() const { return gpio.wasAnyReleased(); } } mappedInput;', fixture, flags=re.S)
fixture = fixture.replace('require(RenderLock::busy, "build without render lock"); ++ticks; clockMs += tickMs;', 'require(RenderLock::busy, "build without render lock"); beforeQuantum(); ++ticks; clockMs += tickMs; afterQuantum();')
fixture = re.sub(r'clockMs \+= ([A-Za-z0-9_]+);', r'fakeble::advanceMillis(\1);', fixture)
assert 'clockMs' not in fixture
# Only the concrete page mutation is outside this scheduler-focused projection.
fixture = fixture.replace('bool latTrangThat(bool);', 'bool latTrangThat(bool) { return true; }')

inp = read('freeink-sdk/libs/hardware/InputManager/src/InputManager.cpp')
input_header = read('freeink-sdk/libs/hardware/InputManager/include/InputManager.h')
assert 'static constexpr int ADC_NO_BUTTON = 3900;' in input_header
assert 'static constexpr unsigned long DEBOUNCE_DELAY = 5;' in input_header
gpio = read('lib/hal/HalGPIO.cpp')
board = read('freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h')
profile = board[board.index('constexpr BoardProfile XTEINK_X4_PRO'):]
assert 'InputStyle::DigitalButtons' in profile[:300]
assert '{PIN_UNASSIGNED, PIN_UNASSIGNED, PIN_UNASSIGNED, PIN_UNASSIGNED, 0, 7, 3, false}' in profile[:3500]
input_code = '\n'.join(function(inp, 'InputManager::' + name) for name in (
    'getButtonFromADC', 'readButtonAdc', 'getState', 'getDigitalState', 'isDigitalPressed',
    'isPowerButtonPhysicallyPressed', 'applyStateChange', 'update', 'wasAnyPressed', 'wasAnyReleased', 'isPressed'))
input_code += '\n' + '\n'.join(re.findall(r'^const int InputManager::ADC_RANGES_\d\[\].*;', inp, re.M))
press_start = gpio.index('struct PressPlan {')
press_end = gpio.index('\n#ifdef TENOR_PRESS_PROBE', press_start)
input_code += '\n' + gpio[press_start:press_end]
input_code += '\n' + function(gpio, 'reportPlannedPresses')
for name in ('injectPresses', 'update', 'rawInputActive'):
    input_code += '\n' + function(gpio, 'HalGPIO::' + name)
# Probe contact support is compiled, while reader probe logging stays outside the projection.
input_code = '#define TENOR_PRESS_PROBE\n' + input_code + '\n#undef TENOR_PRESS_PROBE\n'
functions = '\n'.join(function(cpp, 'EpubReaderActivity::' + name) for name in (
    'deferBackgroundBuildForBle', 'buildTickHeapGate', 'backgroundBuildStartHeapGate',
    'backgroundBuildCanTick', 'backgroundBuildWanted', 'suspendBackgroundBuild', 'pageAwaitsLayout'))
reader = read('src/activities/reader/ReaderActivity.cpp', reader_root)
functions += '\n' + '\n'.join(function(reader, 'ReaderActivity::' + name) for name in ('luotLatTrangNgoai', 'queuePageTurn'))
host_glue = read('src/BlePageTurnerHost.cpp')
manager = read('src/activities/ActivityManager.cpp')
functions += '\n' + function(manager, 'ActivityManager::pageTurn').replace('ActivityManager::pageTurn', 'Manager::pageTurn')
functions += '\nvoid runShortcut(bleturner::Action) { throw std::runtime_error("unexpected shortcut"); }\n'
functions += function(host_glue, 'bleDeliver')
radio = read('lib/BlePageTurner/src/SdkRadio.cpp')
radio = ('namespace bleturner::port {\nfreeink::BleKeyboardHost& hid() { return fakeble::host(); }\n'
         'Peer peerOf(const char* addr, const char* name) { return {addr, name}; }\n' +
         radio[radio.index('bool compiledIn()'):radio.index('bool spawnStart()')] +
         '\nbool spawnStart(){throw std::runtime_error("unexpected radio start");}\n'
         'uint32_t nowMs(){return millis();} void sleepMs(uint32_t ms){fakeble::advanceMillis(ms);}\n'
         'Memo& restartMemo(){static Memo memo{};return memo;}\n}\n')
main = read('src/main.cpp')
assert main.index('  mappedInputManager.update();', main.index('void loop()')) < main.index('    bleInputActivity = bleturner::tick(scene);')
assert main.index('    bleInputActivity = bleturner::tick(scene);') < main.index('  activityManager.loop();', main.index('void loop()'))
source = read('test/look_ahead_inputs/input_fixture.hpp') + '\n' + fixture + '\n' + input_code + '\n' + radio + '\n' + functions + '\n' + scheduler + '\n' + read('test/look_ahead_inputs/cases.cpp')
(out / 'projection.cpp').write_text(source)
sdk = root / 'freeink-sdk/libs/network/BleKeyboardHost'
module = root / 'lib/BlePageTurner'
cmd = [a.compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-fsanitize=address,undefined', '-g', '-pthread', '-DFREEINK_CAP_BLE_HID_HOST=1', '-DBLETURNER_TESTING', '-DENABLE_ARDUINO_FEATURES=0']
for path in (root/'test/ble_page_turner/stubs', sdk/'tests/stubs', sdk/'tests', sdk/'include', sdk/'src', module/'include', module/'src', root/'src'):
    cmd.append('-I' + str(path))
sdk_cmd = cmd + ['-Wno-error=unused-variable', '-c', str(sdk/'src/BleKeyboardHost.cpp'), '-o', str(out/'hid-host.o')]
for path in (out/'projection.cpp', sdk/'tests/FakeBle.cpp', sdk/'src/HidKeymap.cpp', module/'src/Runtime.cpp', out/'hid-host.o'):
    cmd.append(str(path))
cmd += ['-o', str(out/'projection')]
for path in (sdk/'tests/FakeBle.cpp', sdk/'src/BleKeyboardHost.cpp', sdk/'src/HidKeymap.cpp', module/'src/Runtime.cpp', module/'include/BlePageTurner.h'):
    read(path.relative_to(root))
(out / 'manifest.json').write_text(json.dumps({'sources': files, 'command': cmd, 'sdk_command': sdk_cmd}, indent=2))
if a.prepare_only:
    raise SystemExit(0)
sdk_compile = subprocess.run(sdk_cmd, capture_output=True, text=True)
(out / 'sdk-compile.log').write_text(sdk_compile.stdout + sdk_compile.stderr)
if sdk_compile.returncode:
    print(sdk_compile.stdout + sdk_compile.stderr)
    raise SystemExit(sdk_compile.returncode)
compile = subprocess.run(cmd, capture_output=True, text=True)
(out / 'compile.log').write_text(compile.stdout + compile.stderr)
if compile.returncode:
    print(compile.stdout + compile.stderr)
    raise SystemExit(compile.returncode)
run = subprocess.run([str(out/'projection')], capture_output=True, text=True)
(out / 'results.log').write_text(run.stdout + run.stderr)
print(run.stdout + run.stderr, end='')
raise SystemExit(run.returncode)
