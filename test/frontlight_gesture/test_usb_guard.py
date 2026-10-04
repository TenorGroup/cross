"""Compile the actual manager's exclusive-storage loop prefix with spy activities/input.

Only the early routing guard is isolated; physical MSC ownership is a device gate.
"""
from pathlib import Path
import subprocess
import tempfile

source=(Path(__file__).resolve().parents[2]/'src/activities/ActivityManager.cpp').read_text()
start=source.index('void ActivityManager::loop() {')
end=source.index('  if (!sleepTransition',start)
prefix=source[start:end]
fixture=r'''
#include <atomic>
#include <cassert>
#include <cstdint>
int notices=0,normalPath=0;
constexpr int eIncrement=1;
void xTaskNotify(int,int,int){++notices;}
struct Activity { bool exclusive=true; int loops=0;
  bool requiresExclusiveStorageLoop() const{return exclusive;}
  void loop(){++loops;}
};
struct Input {int pops=0; bool released=true;
  bool consumeSuppressedRelease(){return false;}
  bool popMultiTouchSwipe(uint8_t& count,int& dx,int& dy){++pops;count=2;dx=0;dy=-120;released=false;return true;}
};
struct ActivityManager {Input mappedInput; Activity* currentActivity=nullptr;
  std::atomic<bool> requestedUpdate{false}; int renderTaskHandle=1;
  void loop();
};
'''
test=r'''
int main(){
  Activity connected;
  ActivityManager manager;
  manager.currentActivity=&connected;
  manager.loop();
  assert(connected.loops==1 && manager.mappedInput.pops==1 && normalPath==0 && notices==0);
  assert(!manager.mappedInput.released);
  manager.requestedUpdate=true;
  manager.loop();
  assert(connected.loops==2 && normalPath==0 && notices==1);
  connected.exclusive=false;
  manager.loop();
  assert(normalPath==1 && connected.loops==2);
}
'''
with tempfile.TemporaryDirectory(prefix='usb-gesture-guard-') as tmp:
    cpp=Path(tmp)/'guard.cpp';exe=Path(tmp)/'guard'
    cpp.write_text(fixture+prefix+'  ++normalPath;\n}\n'+test)
    subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror',str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('GREEN: actual exclusive-storage guard drains multi-event and stops before normal navigation/save')
