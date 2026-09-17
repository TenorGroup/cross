"""Compile the production TTF command body with probe spies; never raster on the main task."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]
source=(ROOT/'src/main.cpp').read_text()
body=source.split('// CMD:TTF_PROBE ',1)[1].split('\n',1)[1].split('#endif  // TENOR_TTF_PROBE',1)[0]
cpp=r'''
#include <string>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include "TtfProbe.h"
class String {
 std::string s;
public:
 String(std::string x):s(x){}
 String substring(int a) const { return s.substr(a); }
 String substring(int a,int b) const { return s.substr(a,b-a); }
 void trim() { const auto a=s.find_first_not_of(" \t\r\n"); const auto b=s.find_last_not_of(" \t\r\n"); s=a==std::string::npos?"":s.substr(a,b-a+1); }
 int lastIndexOf(char c) const { auto p=s.rfind(c); return p==std::string::npos?-1:static_cast<int>(p); }
 char operator[](int i) const { return s[i]; }
 char charAt(int i) const { return s[i]; }
 int toInt() const { return std::atoi(s.c_str()); }
 const char* c_str() const { return s.c_str(); }
};
int workerCalls=0,directCalls=0,outputs=0;
std::string lastPath;
int lastPt=0,lastBuffer=0;
bool success=false;
struct Log { template<class... T> void printf(const char*, T...) { ++outputs; } } logSerial;
namespace ttfprobe {
 KetQua chayTrenTaskRieng(const char* p,uint8_t pt,uint16_t n,uint32_t) {
   ++workerCalls;lastPath=p;lastPt=pt;lastBuffer=n;
   KetQua q;q.moDuoc=success;return q;
 }
 KetQua chay(const char*,uint8_t,uint16_t) { ++directCalls;return {}; }
}
void dispatch(String cmd) {
'''+body+r'''
}
int main() {
 int failed=0;
 for(int test=0;test<3;++test) {
  workerCalls=directCalls=outputs=0;success=test==1;
  dispatch(String(test==2?"TTF_PROBE missing":"TTF_PROBE /fonts/Book With Spaces.ttf 16 1024"));
  bool ok=directCalls==0 && outputs==1 && workerCalls==(test==2?0:1);
  if(test!=2) ok=ok && lastPath=="/fonts/Book With Spaces.ttf" && lastPt==16 && lastBuffer==1024;
  std::printf("case=%d worker=%d direct=%d outputs=%d %s\n",test,workerCalls,directCalls,outputs,ok?"PASS":"FAIL");
  failed+=!ok;
 }
 return failed?1:0;
}
'''
with tempfile.TemporaryDirectory(prefix='cross-ttf-dispatch-') as tmp:
    p=Path(tmp)
    (p/'test.cpp').write_text(cpp)
    subprocess.run([os.environ.get('CXX', 'c++'),'-std=c++20','-I'+str(ROOT/'lib/FreeTypeLite/include'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    raise SystemExit(subprocess.run([str(p/'test')]).returncode)
