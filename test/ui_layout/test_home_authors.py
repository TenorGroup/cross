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
line_start = source.index('  auto line =', source.index('void HomeActivity::drawTouchRecent('))
line_code = source[line_start:source.index('  int y = f.titleY;', line_start)]
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
display = (display.replace('HalDisplay::BUFFER_SIZE', '48000')
           .replace('return DISPLAY_WIDTH;', 'return 480;')
           .replace('return DISPLAY_HEIGHT;', 'return 800;')
           .replace('return DISPLAY_WIDTH_BYTES;', 'return 60;')
           .replace('return BUFFER_SIZE;', 'return 48000;'))
cpp = display + r'''
#include <FontDecompressor.h>
#include <FontCacheManager.h>
#include <builtinFonts/geist_10_regular.h>
#include <builtinFonts/geist_12_regular.h>
#include <builtinFonts/geist_14_regular.h>
#include <builtinFonts/geist_14_bold.h>
#include <builtinFonts/geist_16_regular.h>
#include <builtinFonts/geist_16_bold.h>
#include <string>
constexpr int UI_10_FONT_ID=1, STR_RECENT_NO_AUTHOR=0;
const char* tr(int) { return "Unknown author"; }
int main() {
  HalDisplay display;
  GfxRenderer renderer(display);
  renderer.begin();
  renderer.setOrientation(GfxRenderer::LandscapeCounterClockwise);
  FontDecompressor decompressor;
  decompressor.init();
  FontCacheManager cache(renderer.getFontMap(),renderer.getSdCardFonts(),renderer.getTtfFonts());
  cache.setFontDecompressor(&decompressor);
  renderer.setFontCacheManager(&cache);
  int bad=0, cases=0;
  bool centered=false;
''' + line_code + r'''
  for (const auto* data : {&geist_10_regular, &geist_12_regular, &geist_14_regular}) {
    EpdFont font(data);
    if(!renderer.replaceBuiltinFont(UI_10_FONT_ID,EpdFontFamily(&font)))
      renderer.insertFont(UI_10_FONT_ID,EpdFontFamily(&font));
    for (const bool pair : {false, true}) for (const int lines : {1, 2}) {
      struct { std::string author; } book{pair ? "Bent Flyvbjerg, Dan Gardner" : "Bent Flyvbjerg"};
      struct { struct {int x=224, w=232;} text; int authorY=204, authorLines=2; } f;
      f.authorLines=lines;
      if(lines==1) { f.text.x=24; f.text.w=432; }
      int y=0;
      std::fill(pixels.begin(), pixels.end(), 0xff);
''' + block + r'''
      const auto actual=pixels;
      assert(std::any_of(actual.begin(),actual.end(),[](uint8_t byte){return byte!=255;}));
      std::fill(pixels.begin(), pixels.end(), 0xff);
      if(pair && lines==2) {
        renderer.drawText(UI_10_FONT_ID,f.text.x,f.authorY,"Bent Flyvbjerg");
        renderer.drawText(UI_10_FONT_ID,f.text.x,f.authorY+renderer.getLineHeight(UI_10_FONT_ID),"Dan Gardner");
      } else renderer.drawText(UI_10_FONT_ID,f.text.x,f.authorY,book.author.c_str());
      if(actual!=pixels) { ++bad; std::printf("RED author raster pair=%d lines=%d height=%d\n",pair,lines,data->advanceY); }
      ++cases;
    }
  }
  centered=true;
  for (const auto pair : {std::pair{&geist_14_regular,&geist_14_bold},std::pair{&geist_16_regular,&geist_16_bold}}) {
    EpdFont regular(pair.first),bold(pair.second);
    renderer.replaceBuiltinFont(UI_10_FONT_ID,EpdFontFamily(&regular,&bold));
    for (const char* text : {"Những việc lớn được hoàn", "Bent Flyvbjerg, Dan Gardner", "15% đã đọc, xong dự kiến >365 ngày"}) {
      std::fill(pixels.begin(),pixels.end(),0xff);
      line(UI_10_FONT_ID,24,100,432,text,EpdFontFamily::BOLD);
      int left=480,right=-1;
      for(int y=0;y<800;++y) for(int x=0;x<480;++x)
        if(!(pixels[y*60+x/8] & (0x80>>(x%8)))) {
          left=std::min(left,x);right=std::max(right,x);
        }
      const double dx=(left+right-renderer.getScreenWidth()+1)/2.0;
      assert(right>=left);
      if(std::abs(dx)>0.5) {++bad;std::printf("RED centered Home text offset=%.1f height=%d\n",dx,pair.first->advanceY);}
      ++cases;
    }
  }
  std::printf("%s author raster cases=%d failures=%d\n",bad?"RED":"GREEN",cases,bad);
  return bad?1:0;
}
'''
path = a.output / 'authors.cpp'
path.write_text(cpp)
include = a.output / 'include'
include.mkdir(exist_ok=True)
(include/'FontDecompressor.h').write_text((root/'lib/EpdFont/FontDecompressor.h').read_text())
includes = ['test/button_symbols/stubs', 'test/sd_card_font/stubs', 'src', 'lib/GfxRenderer', 'lib/EpdFont',
            'lib/Utf8', 'lib/Epub', 'lib/Memory', 'lib/MiniBidi', 'lib/Serialization',
            'lib/hal', 'lib/I18n', 'freeink-sdk/libs/display/FreeInkDisplay/include',
            'lib/InflateReader', 'lib/uzlib/src', 'freeink-sdk/libs/font/FreeInkFont/include']
sources = ['lib/GfxRenderer/GfxRenderer.cpp', 'lib/GfxRenderer/FontCacheManager.cpp',
           'lib/GfxRenderer/InlineSymbols.cpp', 'lib/EpdFont/EpdFont.cpp',
           'lib/EpdFont/EpdFontFamily.cpp', 'lib/EpdFont/SdCardFont.cpp', 'lib/Utf8/Utf8.cpp',
           'lib/Epub/Epub/blocks/TextBlock.cpp', 'lib/MiniBidi/BidiUtils.cpp',
           'lib/EpdFont/FontDecompressor.cpp', 'lib/InflateReader/InflateReader.cpp']
exe = a.output / 'authors'
bidi = a.output / 'minibidi.o'
subprocess.run(['cc', '-O1', '-c', str(root/'lib/MiniBidi/minibidi.c'), '-o', str(bidi)], check=True)
objects=[str(bidi)]
for i,unit in enumerate(['lib/uzlib/src/tinflate.c','freeink-sdk/libs/font/FreeInkFont/src/FontAlloc.c']):
    obj=a.output/f'c{i}.o'
    subprocess.run(['cc','-O1',*['-I'+str(root/d) for d in includes],'-c',str(root/unit),'-o',str(obj)],check=True)
    objects.append(str(obj))
prune = '-Wl,-dead_strip' if sys.platform == 'darwin' else '-Wl,--gc-sections'
subprocess.run(['c++', '-std=c++20', '-O1', '-UNDEBUG', '-ffunction-sections', '-fdata-sections',
                prune, '-I'+str(include), *['-I'+str(root/i) for i in includes], str(path), *objects,
                *[str(root/s) for s in sources], '-o', str(exe)], check=True)
result = subprocess.run([str(exe)])
if a.mutation:
    assert result.returncode == 1, 'embedded-newline author mutation survived'
    print('GREEN: embedded-newline author mutation rejected')
else:
    result.check_returncode()
