from pathlib import Path
import argparse
import subprocess
import hashlib
import json
import sys
import PIL
from PIL import Image, ImageDraw, ImageFont


def function(text, signature):
    start = text.index(signature)
    opening = text.index('{', start)
    end = opening + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--ref')
parser.add_argument('--ink-file', type=Path)
parser.add_argument('--benchmark', action='store_true')
args = parser.parse_args()
repo = Path(__file__).resolve().parents[2]
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
font = ImageFont.truetype(str(repo / 'lib/EpdFont/builtinFonts/source/Mansalva/Mansalva-Regular.ttf'), 30)
fixtures = []
glyph_parts = ['struct TextFixture { const unsigned char* ink; int width, height; };']
for index, text in enumerate(['Chon dong nay', 'Gửi gió', 'Ỷ lại', 'Chọn dòng này', 'File', 'Nhật ký',
                              'Những ngày đọc sách', 'đang đọc dở']):
    bounds = font.getbbox(text)
    mask = Image.new('1', (bounds[2] - bounds[0], bounds[3] - bounds[1]))
    ImageDraw.Draw(mask).text((-bounds[0], -bounds[1]), text, font=font, fill=1)
    pixels = mask.get_flattened_data() if hasattr(mask, 'get_flattened_data') else mask.getdata()
    glyph_parts.append('static constexpr unsigned char glyph' + str(index) + '[] = {' +
                       ','.join(str(int(pixel != 0)) for pixel in pixels) + '};')
    fixtures.append('{glyph' + str(index) + ',' + str(mask.width) + ',' + str(mask.height) + '}')
glyph_parts.append('static constexpr TextFixture textFixtures[] = {' + ','.join(fixtures) + '};')
(out / 'glyphs.h').write_text('\n'.join(glyph_parts))
(out / 'glyph-manifest.json').write_text(json.dumps({'python': sys.version, 'pillow': PIL.__version__,
    'font_sha256': hashlib.sha256((repo / 'lib/EpdFont/builtinFonts/source/Mansalva/Mansalva-Regular.ttf').read_bytes()).hexdigest(),
    'glyphs_sha256': hashlib.sha256((out / 'glyphs.h').read_bytes()).hexdigest()}, indent=2) + '\n')
def production(path):
    if args.ref:
        return subprocess.check_output(['git', '-C', str(repo), 'show', args.ref + ':' + path], text=True)
    return (repo / path).read_text()


ink = args.ink_file.read_text() if args.ink_file else production('src/shells/ugly/UglyInk.cpp')
logic = production('src/shells/ugly/UglyLogic.h')
if 'inline int wobble(' not in logic:
    logic = production('src/shells/ugly/UglyWobble.h')
sheet = production('src/shells/ugly/UglyQuestionSheet.cpp')
circle = function(ink, 'void circle(')
seeded = 'uint32_t seed' in circle
parts = ['#include <algorithm>\n#include <array>\n#include <atomic>\n#include <chrono>\n#include <cstdint>\n#include <cstdlib>\n#include <cstdio>\n#include <cstring>\n#include <new>\n#include <set>\n#include <string>\n',
         '''static int allocations = 0;
void* operator new(size_t size) { ++allocations; if (auto* memory = malloc(size)) return memory; throw std::bad_alloc(); }
void* operator new[](size_t size) { return ::operator new(size); }
void* operator new(size_t size, std::align_val_t align) { ++allocations; void* memory=nullptr; if (!posix_memalign(&memory, static_cast<size_t>(align), size)) return memory; throw std::bad_alloc(); }
void* operator new[](size_t size, std::align_val_t align) { return ::operator new(size, align); }
void operator delete(void* memory) noexcept { free(memory); }
void operator delete[](void* memory) noexcept { free(memory); }
void operator delete(void* memory, std::align_val_t) noexcept { free(memory); }
void operator delete[](void* memory, std::align_val_t) noexcept { free(memory); }
inline void* countedMalloc(size_t size) { ++allocations; return malloc(size); }
inline void* countedCalloc(size_t count,size_t size) { ++allocations; return calloc(count,size); }
inline void* countedRealloc(void* memory,size_t size) { ++allocations; return realloc(memory,size); }
inline void* countedAlignedAlloc(size_t alignment,size_t size) { ++allocations; return aligned_alloc(alignment,size); }
inline int countedPosixMemalign(void** memory,size_t alignment,size_t size) { ++allocations; return posix_memalign(memory,alignment,size); }
''',
         'class GfxRenderer { public: mutable unsigned char pixels[528*1120]{}; int screenWidth=528,screenHeight=792; mutable int outside=0; int getScreenWidth() const { return screenWidth; } int getScreenHeight() const { return screenHeight; } void fillRect(int x,int y,int width,int height,bool black=true) const { for(int row=y;row<y+height;++row) for(int col=x;col<x+width;++col) if(col>=0&&col<screenWidth&&row>=0&&row<screenHeight) pixels[row*screenWidth+col]=black; else ++outside; } void drawPixel(int x,int y) const { fillRect(x,y,1,1); } };\n',
         '#include "UglyTables.h"\n']
if seeded:
    parts.append('#include "UglyMarkSeed.h"\n')
parts.append('#include <cmath>\n')
parts += ['namespace ugly { struct Box { int x0,y0,x1,y1; }; enum class Circle {Word,Object,Row};\nnamespace logic {',
          function(logic, 'inline int wobble('), '}\n',
          '#define malloc countedMalloc\n#define calloc countedCalloc\n#define realloc countedRealloc\n#define aligned_alloc countedAlignedAlloc\n#define posix_memalign countedPosixMemalign\n',
          function(ink, 'void dot(const GfxRenderer& r, const int x'),
          function(ink, 'void stroke(const GfxRenderer& r, int x0, int y0, const int x1').replace('void stroke(', 'void rasterStroke(', 1),
          '''struct Segment { int x0, y0, x1, y1; };
static Segment segments[128];
static int segmentCount = 0;
void stroke(const GfxRenderer& r, int x0, int y0, int x1, int y1, int width) {
  if (segmentCount < 128) segments[segmentCount] = {x0, y0, x1, y1};
  ++segmentCount;
  rasterStroke(r, x0, y0, x1, y1, width);
}''',
          function(ink, 'void line(const GfxRenderer& r, const int x0'), circle]
parts.append((Path(__file__).parent / 'base_circle.cpp').read_text())
if not seeded:
    parts.append('void seededCircle(const GfxRenderer& renderer,Circle role,const Box& box,int px,int py,uint32_t,int width) { circle(renderer,role,box,px,py,width); }')
else:
    parts.append('void seededCircle(const GfxRenderer& renderer,Circle role,const Box& box,int px,int py,uint32_t seed,int width) { circle(renderer,role,box,px,py,seed,width); }')
pencil_source = ink if 'void pencil(' in ink else sheet
parts += [function(pencil_source, 'uint32_t noise('), function(pencil_source, 'void pencil('), '}\n',
          '#undef malloc\n#undef calloc\n#undef realloc\n#undef aligned_alloc\n#undef posix_memalign\n',
          '#include "glyphs.h"\n', (Path(__file__).parent / 'marks.cpp').read_text()]
generated = out / 'production.cpp'
generated.write_text('\n'.join(parts))
(out / 'manifest.json').write_text(json.dumps({'ref': args.ref, 'ink_sha256': hashlib.sha256(ink.encode()).hexdigest(),
                                            'sheet_sha256': hashlib.sha256(sheet.encode()).hexdigest(),
                                            'base_fixture_sha256': hashlib.sha256((Path(__file__).parent / 'base_circle.cpp').read_bytes()).hexdigest(),
                                            'tables_sha256': hashlib.sha256((repo / 'src/shells/ugly/UglyTables.h').read_bytes()).hexdigest(),
                                            'extracted_sha256': hashlib.sha256(generated.read_bytes()).hexdigest()}, indent=2) + '\n')
command = ['c++', '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
           '-I' + str(repo / 'src/shells/ugly'), str(generated), '-o', str(out / 'marks')]
(out / 'command.txt').write_text(' '.join(command) + '\n')
build = subprocess.run(command, capture_output=True, text=True)
(out / 'build.log').write_text(build.stdout + build.stderr)
if build.returncode:
    print(build.stdout + build.stderr)
    raise SystemExit(build.returncode)
run_command = [str(out / 'marks'), str(out)]
if args.benchmark:
    run_command.append('--benchmark')
run = subprocess.run(run_command, capture_output=True, text=True)
(out / 'run.log').write_text(run.stdout + run.stderr)
print(run.stdout + run.stderr, end='')
raise SystemExit(run.returncode)
