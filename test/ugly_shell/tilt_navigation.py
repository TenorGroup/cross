"""Inject HAL events through the production ugly screen loop and busy-panel queue."""
import argparse
from pathlib import Path
import subprocess


def method(source, signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


parser = argparse.ArgumentParser()
parser.add_argument('--repo', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
source = (args.repo / 'src/shells/ugly/UglyScreen.cpp').read_text()
stubs = args.output / 'stubs'
stubs.mkdir(exist_ok=True)
(stubs / 'CrossPointSettings.h').write_text('''#pragma once
#include <cstdint>
struct Settings { uint8_t tiltTabNavigation=1, tiltMenuNavigation=1; };
extern Settings SETTINGS;
''')
(stubs / 'HalTiltSensor.h').write_text('''#pragma once
#include <cstdint>
namespace CrossPointTiltPageTurn { enum { TILT_OFF=0 }; }
struct HalTiltSensor {
  int polls=0, orientation=-1; uint8_t tabs=0, rows=0; bool tabActive=false, rowActive=false;
  bool forward=false, back=false, up=false, down=false;
  void update(uint8_t mode,uint8_t angle,bool active) { ++polls; tabs=mode; orientation=angle; tabActive=active; }
  void configureVerticalGesture(uint8_t mode,bool active) { rows=mode; rowActive=active; }
  bool take(bool& event,uint8_t mode,bool active) { bool seen=event; event=false; return seen && mode && active; }
  bool wasTiltedForward() { return take(tabs==2?back:forward,tabs,tabActive); }
  bool wasTiltedBack() { return take(tabs==2?forward:back,tabs,tabActive); }
  bool wasTiltedUp() { return take(rows==2?down:up,rows,rowActive); }
  bool wasTiltedDown() { return take(rows==2?up:down,rows,rowActive); }
};
extern HalTiltSensor halTiltSensor;
''')
cpp = r'''
#include <cassert>
#include <cstdio>
#include <functional>
#include <vector>
#include <CrossPointSettings.h>
#include <HalTiltSensor.h>
Settings SETTINGS; HalTiltSensor halTiltSensor;
'''
if (args.repo / 'src/MenuTiltInput.h').exists():
    cpp += '#include "MenuTiltInput.h"\n'
cpp += r'''
struct MappedInputManager {
  enum class Button { Left,Right,Up,Down,Confirm,Back };
  bool wasLongPressed(Button,unsigned long) { return false; }
  bool wasReleased(Button) { return false; }
};
bool panelFree=true;
struct RenderLock { struct TryTake {}; RenderLock(TryTake) {} bool acquired() const { return panelFree; } };
namespace ugly {
struct Screen {
  enum class Key { Up,Down,Left,Right,Confirm,Back,UpHold,DownHold };
  static constexpr int QUEUE=8; static constexpr unsigned long HOLD_MS=700;
  Key queue[QUEUE]; uint8_t head=0,count=0; std::function<void()> later;
  MappedInputManager mappedInput;
  struct Renderer { int getOrientation() const { return 3; } } renderer;
  bool tabbed=false; int refreshes=0; std::vector<Key> seen;
  bool acceptsTiltTabNavigation() const { return tabbed; }
  void push(Key); void loop();
  bool onKey(Key key) { seen.push_back(key); return true; }
  void afterKeys() {} void requestUpdate() { ++refreshes; }
};
'''
cpp += method(source, 'void Screen::push(const Key key)') + '\n'
cpp += method(source, 'void Screen::loop()') + '\n}\n'
cpp += r'''
namespace shell { bool uglySelected=true; bool isUgly() { return uglySelected; } }
namespace ugly { struct QuestionSheet { enum class Key { PreviousQuestion,NextQuestion }; }; }
struct UiListActivity { int crossPolls=0; void pollTilt() { ++crossPolls; } };
struct UiTabListActivity : UiListActivity {};
struct FormStub {
  ugly::Screen::Renderer renderer; int settingsCount=4,visibleItemCount=6;
  struct FormEvent { enum class Type { Key }; Type type; ugly::QuestionSheet::Key key; };
  std::vector<ugly::QuestionSheet::Key> seen;
  void queueForm(FormEvent event) { seen.push_back(event.key); }
};
'''
for activity, base in (('SettingsActivity', 'UiTabListActivity'),
                       ('TextSettingsActivity', 'UiTabListActivity'),
                       ('StatusBarSettingsActivity', 'UiListActivity')):
    cpp += f'struct {activity} : {base}, FormStub {{ void pollTilt(); }};\n'
    activity_source = (args.repo / f'src/activities/settings/{activity}.cpp').read_text()
    cpp += method(activity_source, f'void {activity}::pollTilt()') + '\n'
cpp += r'''
template<class Form> bool checkForm(const char* name) {
  for (int mode : {1,0,2}) {
    SETTINGS.tiltMenuNavigation=mode; SETTINGS.tiltTabNavigation=1; halTiltSensor={};
    halTiltSensor.up=true; Form form; form.pollTilt();
    if (form.seen.size()!=(mode?1u:0u)) {
      fprintf(stderr,"RED form=%s mode=%d moves=%zu\n",name,mode,form.seen.size()); return false;
    }
    if (mode) assert(form.seen[0]==(mode==2?ugly::QuestionSheet::Key::PreviousQuestion:ugly::QuestionSheet::Key::NextQuestion));
    assert(halTiltSensor.polls==1 && halTiltSensor.orientation==3 && halTiltSensor.tabs==0);
    form.pollTilt(); assert(form.seen.size()==(mode?1u:0u));
    halTiltSensor.forward=true; form.pollTilt(); assert(form.seen.size()==(mode?1u:0u));
    shell::uglySelected=false; form.pollTilt(); assert(form.crossPolls==1); shell::uglySelected=true;
  }
  return true;
}
'''
cpp += r'''
int main() {
  using Screen=ugly::Screen; using Key=Screen::Key;
  for (int mode : {1,0,2}) for (bool tabbed : {false,true}) for (int axis : {2,3,0,1}) {
    Screen screen; screen.tabbed=tabbed; halTiltSensor={};
    SETTINGS.tiltTabNavigation=mode; SETTINGS.tiltMenuNavigation=mode;
    if (axis==0) halTiltSensor.forward=true;
    if (axis==1) halTiltSensor.back=true;
    if (axis==2) halTiltSensor.up=true;
    if (axis==3) halTiltSensor.down=true;
    screen.loop();
    const bool expected=mode!=0 && (axis>=2 || tabbed);
    if (screen.seen.size()!=(expected?1u:0u)) {
      fprintf(stderr,"RED mode=%d tabbed=%d axis=%d moves=%zu\n",mode,tabbed,axis,screen.seen.size()); return 1;
    }
    if (expected) {
      Key normal=axis==0?Key::Right:axis==1?Key::Left:axis==2?Key::Down:Key::Up;
      Key inverted=axis==0?Key::Left:axis==1?Key::Right:axis==2?Key::Up:Key::Down;
      assert(screen.seen[0]==(mode==2?inverted:normal));
    }
    assert(halTiltSensor.polls==1 && halTiltSensor.orientation==3);
    assert(halTiltSensor.tabs==(tabbed?mode:0));
    screen.loop(); assert(screen.seen.size()==(expected?1u:0u));
  }
  Screen busy; SETTINGS.tiltMenuNavigation=1; halTiltSensor={}; halTiltSensor.up=true;
  panelFree=false; busy.loop(); assert(busy.seen.empty() && busy.count==1);
  panelFree=true; busy.loop(); assert(busy.seen.size()==1 && busy.seen[0]==Key::Down);
  if (!checkForm<SettingsActivity>("Settings") || !checkForm<TextSettingsActivity>("Text") ||
      !checkForm<StatusBarSettingsActivity>("StatusBar")) return 1;
  puts("GREEN 24 screen routes and 9 form routes, single poll, orientation, consumption, busy-panel replay");
}
'''
translation_unit = args.output / 'tilt.cpp'
translation_unit.write_text(cpp)
program = args.output / 'tilt'
subprocess.run(['c++', '-std=c++20', '-I' + str(stubs), '-I' + str(args.repo / 'src'),
                str(translation_unit), '-o', str(program)], check=True)
subprocess.run([str(program)], check=True)
