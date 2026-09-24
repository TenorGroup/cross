"""The one "more this way" mark, tenorchrome::drawMoreChevron, drawn through a recording renderer.

An open V, never filled (filled triangles stand for the physical buttons), with a stroke about
2.4 px across each arm: the two-pass chevron measured 1.7 px and read faint beside bold text.
Left is the mirror of right pixel for pixel, and the down chevron is symmetric about its tip.
"""
from pathlib import Path
import math, re, subprocess, tempfile
ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'src/components/TenorMenuChrome.cpp').read_text()
header = (ROOT / 'src/components/TenorMenuChrome.h').read_text()


def function(text, name):
    start = text.rfind('\n', 0, text.index(name)) + 1
    opening = text.index('{', start); end = opening + 1; depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}'); end += 1
    return text[start:end]


declarations = '\n'.join(re.findall(r'(?:enum class ChevronDir[^;]+;|constexpr int MORE_CHEVRON_[^;]+;|'
                                    r'constexpr int moreChevronLength[^}]+})', header))
fixture = r"""
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <utility>
struct GfxRenderer {
 mutable std::set<std::pair<int,int>> ink;
 void drawLine(int x,int y,int endX,int endY,bool=true) const {
   int dx=std::abs(endX-x),sx=x<endX?1:-1,dy=-std::abs(endY-y),sy=y<endY?1:-1,err=dx+dy;
   for(;;) { ink.emplace(x,y); if(x==endX&&y==endY)break; int e=2*err;
     if(e>=dy){err+=dy;x+=sx;} if(e<=dx){err+=dx;y+=sy;}
   }
 }
};
namespace tenorchrome {
// DECLARATIONS
void drawMoreChevron(const GfxRenderer&, int, int, ChevronDir, int);
}
// PRODUCTION
int main(int, char** argv) {
 const int span = std::atoi(argv[1]);
 for (int d = 0; d < 3; ++d) {
   GfxRenderer r;
   tenorchrome::drawMoreChevron(r, 100, 100, static_cast<tenorchrome::ChevronDir>(d), span);
   for (auto [x, y] : r.ink) std::printf("%d %d %d\n", d, x - 100, y - 100);
 }
 std::printf("L %d\n", tenorchrome::moreChevronLength(span));
}
"""
fixture = fixture.replace('// DECLARATIONS', declarations).replace(
    '// PRODUCTION', function(source, 'void tenorchrome::drawMoreChevron('))
failures = []
with tempfile.TemporaryDirectory(prefix='more-chevron-') as temp:
    src = Path(temp) / 'test.cpp'; exe = Path(temp) / 'test'; src.write_text(fixture)
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-UNDEBUG', str(src), '-o', str(exe)],
                   check=True)
    for span in (5, 7, 11):
        out = subprocess.run([str(exe), str(span)], check=True, capture_output=True, text=True).stdout.split('\n')
        length = int(next(line for line in out if line.startswith('L ')).split()[1])
        ink = {0: set(), 1: set(), 2: set()}
        for line in out:
            parts = line.split()
            if len(parts) == 3:
                ink[int(parts[0])].add((int(parts[1]), int(parts[2])))
        down, left, right = ink[0], ink[1], ink[2]
        across = 2 * span + 1
        # The box the callers lay out with.
        xs = [x for x, _ in down]; ys = [y for _, y in down]
        if (min(xs), max(xs), min(ys), max(ys)) != (0, across - 1, 0, length - 1):
            failures.append(f'span {span}: down box {min(xs)}..{max(xs)} x {min(ys)}..{max(ys)}')
        for name, pts in (('left', left), ('right', right)):
            xs = [x for x, _ in pts]; ys = [y for _, y in pts]
            if (min(xs), max(xs), min(ys), max(ys)) != (0, length - 1, 0, across - 1):
                failures.append(f'span {span}: {name} box {min(xs)}..{max(xs)} x {min(ys)}..{max(ys)}')
        # Mirror images: left is right flipped across the box, down is symmetric about its tip.
        if left != {(length - 1 - x, y) for x, y in right}:
            failures.append(f'span {span}: left is not the mirror of right')
        if down != {(across - 1 - x, y) for x, y in down}:
            failures.append(f'span {span}: down is not symmetric')
        # Down and right are the same shape turned a quarter.
        if right != {(y, x) for x, y in down}:
            failures.append(f'span {span}: right is not down turned')
        # Open: every column of the down V is one run of ink along the pointing axis, the stroke, and
        # the rows above the tip in the middle column stay paper.
        stroke = set()
        for x in range(across):
            column = sorted(y for cx, y in down if cx == x)
            if column != list(range(column[0], column[0] + len(column))):
                failures.append(f'span {span}: column {x} broken {column}')
            stroke.add(len(column))
        if stroke != {3}:
            failures.append(f'span {span}: stroke along the axis {sorted(stroke)}, want 3')
        tip = min(y for x, y in down if x == span)
        if tip < length - 3 or any((span, y) in down for y in range(tip)):
            failures.append(f'span {span}: filled above the tip')
        # Across the arm: 3 px along the axis over an arm that rises depth in span.
        depth = length - 3
        across_arm = 3 * span / math.hypot(span, depth)
        if not 2.2 <= across_arm <= 3.0:
            failures.append(f'span {span}: stroke across the arm {across_arm:.2f} px')
        # The arm reaches the edge of the box at both ends: no gap between runs of neighbouring columns.
        for x in range(across - 1):
            a = [y for cx, y in down if cx == x]; b = [y for cx, y in down if cx == x + 1]
            if max(a) < min(b) - 1 or max(b) < min(a) - 1:
                failures.append(f'span {span}: gap between columns {x} and {x + 1}')
print('\n'.join(failures) or 'more chevron: 3 spans x 3 directions ok')
raise SystemExit(1 if failures else 0)
