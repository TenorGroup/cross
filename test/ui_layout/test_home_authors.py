"""Render the production Home author block with real glyphs and a framebuffer."""
import argparse
import subprocess
import sys
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--output', type=Path, required=True)
p.add_argument('--mutation', action='store_true')
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
a.output.mkdir(parents=True, exist_ok=True)
source = (root / 'src/activities/home/HomeActivity.cpp').read_text()
start = source.index('  y = f.authorY;', source.index('void HomeActivity::drawTouchRecent('))
block = source[start:source.index('  char progress[128];', start)]
if a.mutation:
    block = '''
  y = f.authorY;
  auto author = book.author;
  const auto comma = author.find(", ");
  if (f.authorLines > 1 && comma != std::string::npos) author.replace(comma, 2, "\\n");
  for (const auto& text : renderer.wrappedText(UI_10_FONT_ID, author.c_str(), f.text.w, f.authorLines)) {
    line(UI_10_FONT_ID, f.text.x, y, f.text.w, text, EpdFontFamily::REGULAR);
    y += renderer.getLineHeight(UI_10_FONT_ID);
  }
'''
display = (root / 'test/ui_layout/home_cover_edges.cpp').read_text().split('bool ink(')[0]
cpp = display + r'''
#include <builtinFonts/geist_10_regular.h>
#include <builtinFonts/geist_12_regular.h>
#include <builtinFonts/geist_14_regular.h>
#include <string>
constexpr int UI_10_FONT_ID=1, STR_RECENT_NO_AUTHOR=0;
const char* tr(int) { return "Unknown author"; }
int main() {
  HalDisplay display;
  GfxRenderer renderer(display);
  renderer.begin();
  renderer.setOrientation(GfxRenderer::LandscapeCounterClockwise);
  int bad=0, cases=0;
  for (const auto* data : {&geist_10_regular, &geist_12_regular, &geist_14_regular}) {
    EpdFont font(data);
    renderer.insertFont(UI_10_FONT_ID, EpdFontFamily(&font));
    for (const bool pair : {false, true}) for (const int lines : {1, 2}) {
      struct { std::string author; } book{pair ? "Bent Flyvbjerg, Dan Gardner" : "Bent Flyvbjerg"};
      struct { struct {int x=224, w=232;} text; int authorY=204, authorLines=2; } f;
      f.authorLines=lines;
      if(lines==1) { f.text.x=24; f.text.w=432; }
      int y=0;
      std::fill(pixels.begin(), pixels.end(), 0xff);
      auto line=[&](int id,int x,int top,int width,const std::string& text,EpdFontFamily::Style style) {
        renderer.drawText(id,x,top,renderer.truncatedText(id,text.c_str(),width,style).c_str(),true,style);
      };
''' + block + r'''
      const auto actual=pixels;
      std::fill(pixels.begin(), pixels.end(), 0xff);
      if(pair && lines==2) {
        renderer.drawText(UI_10_FONT_ID,f.text.x,f.authorY,"Bent Flyvbjerg");
        renderer.drawText(UI_10_FONT_ID,f.text.x,f.authorY+renderer.getLineHeight(UI_10_FONT_ID),"Dan Gardner");
      } else renderer.drawText(UI_10_FONT_ID,f.text.x,f.authorY,book.author.c_str());
      if(actual!=pixels) { ++bad; std::printf("RED author raster pair=%d lines=%d height=%d\n",pair,lines,data->advanceY); }
      ++cases;
    }
  }
  std::printf("%s author raster cases=%d failures=%d\n",bad?"RED":"GREEN",cases,bad);
  return bad?1:0;
}
'''
path = a.output / 'authors.cpp'
path.write_text(cpp)
includes = ['test/button_symbols/stubs', 'test/sd_card_font/stubs', 'src', 'lib/GfxRenderer', 'lib/EpdFont',
            'lib/Utf8', 'lib/Epub', 'lib/Memory', 'lib/MiniBidi', 'lib/Serialization',
            'lib/hal', 'lib/I18n', 'freeink-sdk/libs/display/FreeInkDisplay/include']
sources = ['lib/GfxRenderer/GfxRenderer.cpp', 'lib/GfxRenderer/FontCacheManager.cpp',
           'lib/GfxRenderer/InlineSymbols.cpp', 'lib/EpdFont/EpdFont.cpp',
           'lib/EpdFont/EpdFontFamily.cpp', 'lib/EpdFont/SdCardFont.cpp', 'lib/Utf8/Utf8.cpp',
           'lib/Epub/Epub/blocks/TextBlock.cpp', 'lib/MiniBidi/BidiUtils.cpp']
exe = a.output / 'authors'
bidi = a.output / 'minibidi.o'
subprocess.run(['cc', '-O1', '-c', str(root/'lib/MiniBidi/minibidi.c'), '-o', str(bidi)], check=True)
prune = '-Wl,-dead_strip' if sys.platform == 'darwin' else '-Wl,--gc-sections'
subprocess.run(['c++', '-std=c++20', '-O1', '-UNDEBUG', '-ffunction-sections', '-fdata-sections',
                prune, *['-I'+str(root/i) for i in includes], str(path), str(bidi),
                *[str(root/s) for s in sources], '-o', str(exe)], check=True)
result = subprocess.run([str(exe)])
if a.mutation:
    assert result.returncode == 1, 'embedded-newline author mutation survived'
    print('GREEN: embedded-newline author mutation rejected')
else:
    result.check_returncode()
