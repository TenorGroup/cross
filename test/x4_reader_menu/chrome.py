"""Execute production reader footbar metadata and rendering on host boundaries."""
import argparse
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[2])
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
source = (args.repo / 'src/components/TenorMenuChrome.cpp').read_text()
header = (args.repo / 'src/components/TenorMenuChrome.h').read_text()

def method(text, signature):
    start = text.index(signature)
    brace = text.index('{', start)
    end, depth = brace + 1, 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

note = re.search(r'struct ReaderFootBarNote \{.*?\} readerFootBar;', source, re.S)
assert note, 'RED: reader metadata missing'
cpp = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#define FREEINK_DEVICE_X4PRO 1
struct Manager {
  uint32_t generation=1; const char* name="EpubReader";
  uint32_t activityGeneration() const { return generation; }
  const char* currentName() const { return name; }
} activityManager;
namespace HeaderBackTapTarget {
  int foot=0, zone=0;
  void clearFoot() { foot=zone=0; }
  void setFoot(int,int,int,int) { ++foot; }
  void setZone(int,int,int,int) { ++zone; }
}
namespace freeink { struct Icon {}; }
freeink::Icon icon_tenor_reader_position_40,icon_tenor_reader_reading_40,icon_tenor_reader_tools_40;
freeink::Icon icon_tenor_reader_position_bold_40,icon_tenor_reader_reading_bold_40,icon_tenor_reader_tools_bold_40;
struct EpdFontFamily { enum Style { REGULAR }; };
constexpr int UI_12_FONT_ID=12;
struct GfxRenderer {
  int width=480,height=800;
  int getScreenWidth() const { return width; }
  int getScreenHeight() const { return height; }
  std::string truncatedText(int,const char* s,int,EpdFontFamily::Style) const { return s; }
  int getTextWidth(int,const char*,EpdFontFamily::Style=EpdFontFamily::REGULAR) const { return 8; }
  int getLineHeight(int) const { return 24; }
  void fillRect(int,int,int,int,bool) const {}
  void drawText(int,int,int,const char*,bool,EpdFontFamily::Style) const {}
};
int rings=0,icons=0;
void drawIcon(const GfxRenderer&,const freeink::Icon&,int,int) { ++icons; }
namespace tenorchrome {
enum class FootBar { None,Tabs,Full,BackOnly };
enum class Zone { Recent,Book };
constexpr int FOOT_BACK_SIZE=60,FOOT_BACK_X=16,FOOT_PILL_GAP=8;
int footBackTop(int h) { return h-76; }
void drawPillRing(const GfxRenderer&,int,int,int,int,int,bool) { ++rings; }
enum class ChevronDir { Left };
int moreChevronLength(int) { return 16; }
void drawMoreChevron(const GfxRenderer&,int,int,ChevronDir,int) {}
const freeink::Icon& zoneIcon(Zone) { return icon_tenor_reader_reading_40; }
const char* screenTitle() { return ""; }
void noteReaderFootBar(bool,bool,int);
void drawFootBar(const GfxRenderer&,FootBar,Zone);
''' + method(header, 'struct ReaderToolRect') + ';\n' + method(header, 'inline ReaderToolRect readerToolRect') + '\n}\n'
cpp += note[0] + '\n' + method(source, 'bool readerFootBarActive()') + '\n'
cpp += method(source, 'void tenorchrome::noteReaderFootBar') + '\n'
cpp += method(source, 'void tenorchrome::drawFootBar') + r'''
int main() {
  using namespace tenorchrome;
  GfxRenderer r;
  auto draw=[&] { rings=icons=0; drawFootBar(r,FootBar::None,Zone::Book); };
  noteReaderFootBar(false,false,-1); draw(); assert(rings==0 && HeaderBackTapTarget::foot==0);
  noteReaderFootBar(true,false,1); draw(); assert(rings==3 && icons==3 && HeaderBackTapTarget::foot==1);
  assert(HeaderBackTapTarget::zone==0);
  noteReaderFootBar(true,true,1); draw(); assert(rings==1 && icons==0 && HeaderBackTapTarget::foot==1);
  noteReaderFootBar(false,false,-1); assert(HeaderBackTapTarget::foot==0); draw(); assert(rings==0);
  noteReaderFootBar(true,false,1); ++activityManager.generation; draw(); assert(rings==0 && HeaderBackTapTarget::foot==0);
  noteReaderFootBar(true,false,1); activityManager.name="Settings"; draw(); assert(rings==0 && HeaderBackTapTarget::foot==0);
  activityManager.name="EpubReader"; noteReaderFootBar(true,false,-1); draw(); assert(rings==2 && icons==3);
  for (int w : {480,800}) for (int i=0;i<3;++i) {
    auto cell=readerToolRect(w,w==480?800:480,i);
    assert(cell.width>=60 && cell.height==60 && cell.y==(w==480?724:404));
    assert(cell.x>=84 && cell.x+cell.width<=w-16);
  }
}
'''
(args.output / 'chrome.cpp').write_text(cpp)
subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                str(args.output / 'chrome.cpp'), '-o', str(args.output / 'chrome')], check=True)
subprocess.run([str(args.output / 'chrome')], check=True)
print('GREEN reader footbar: closed/open/keypad/generation/activity replacement and shared geometry')
