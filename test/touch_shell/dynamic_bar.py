"""Check the production touch bar policy at every UI text size."""
import argparse
import json
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser()
p.add_argument('--work', type=Path, required=True)
p.add_argument('--source-ref')
p.add_argument('--mutate', action='store_true')
a = p.parse_args()
a.work.mkdir(parents=True, exist_ok=True)

def read(path):
    if a.source_ref:
        return subprocess.check_output(['git', 'show', f'{a.source_ref}:{path}'], cwd=ROOT, text=True)
    return (ROOT / path).read_text()

def function(s, name):
    start = s.rfind('\n', 0, s.index(name)) + 1
    pos = s.index('{', start)
    depth, end = 1, pos + 1
    while depth:
        depth += (s[end] == '{') - (s[end] == '}')
        end += 1
    return s[start:end]

h = read('src/components/TenorMenuChrome.h')
c = read('src/components/TenorMenuChrome.cpp')
u = read('src/activities/UiTabListActivity.cpp')
arrays = '\n'.join(re.findall(r'constexpr const char\* (?:FULL_BAR|BACK_ONLY_BAR|TABS_BAR)\[\] = \{.*?\};', c, re.S))
names = re.findall(r'"([A-Za-z]+)"', arrays)
assert len(names) == len(set(names))
shared = 'struct TouchBarRect' in h
if a.mutate:
    assert shared, 'mutation requires shared bar rect'
    original = function(h, 'inline TouchBarRect touchTabBarRect(')
    changed = original.replace('FOOT_BACK_SIZE', '(SETTINGS.uiTextSize == 1 ? 67 : FOOT_BACK_SIZE)')
    h = h.replace(original, changed)
(a.work / 'TenorMenuChrome.h').write_text(h)
legacy = '' if shared else function(u, 'fui::Rect touchTabBarRect(')
bar_band = re.search(r'const int16_t tabBand = .*?;', u, re.S).group()
centres = function(u, 'void UiTabListActivity::veThanhTheTenor(')
# Use the same placement statements as the painter, with the full bar as its input.
centre_expression = re.search(r'cx = tenorchrome::barTabCentre[^;]+;', centres).group().replace('cx =', 'const int cx =')
placement = centres[centres.index('  const int endInset'):centres.index('  fui::TextStyle label')]
size_constants = '\n'.join(re.findall(r'constexpr int (?:LE|KHE|PILL_ICON_W) = [^;]+;', centres))
code = r'''
#include <cstdio>
#include <cstring>
#include <cstdint>
#include "TenorMenuChrome.h"
HalGPIO gpio;
namespace fui { struct Rect { int16_t x,y,width,height; int bottom() const { return y+height; } }; }
namespace shell { bool uglyParts() { return false; } }
'''
code += arrays + '\ntemplate <size_t N>\n' + function(c, 'bool named(') + '\n' + function(c, 'tenorchrome::FootBar tenorchrome::footBarFor(') + '\n' + legacy
code += '\nint main() { int errors=0;\n'
code += 'const char* names[] = {' + ','.join('"'+n+'"' for n in names) + '};\n'
code += r'''
for (int tier=0;tier<3;++tier) {
 SETTINGS.uiTextSize=tier;
 for (const char* name:names) {
  if (tenorchrome::footBarFor(name)==tenorchrome::FootBar::None) ++errors;
  int x=16,y=tenorchrome::footBackTop(800),w=448,h=60;
  if (!std::strcmp(name,"Home") || !std::strcmp(name,"TextSettings") || !std::strcmp(name,"Library") || !std::strcmp(name,"EpubReaderMenu")) {
    const int16_t tabLineHeight=uiTextSizeSpec(tier).bodyLineHeight;
    const int16_t preferredTabHeight=!std::strcmp(name,"Home")?tenorchrome::tabHeight():60;
'''
code += bar_band + '\n'
if shared:
    code += 'const auto bar=tenorchrome::touchTabBarRect(480,800); x=bar.x;y=bar.y;w=bar.width;h=bar.height; if(tabBand!=60) ++errors;\n'
else:
    code += 'const auto bar=touchTabBarRect({0,0,480,800},false,tabBand); x=bar.x+8;y=bar.y;w=bar.width-16;h=bar.height;\n'
code += r'''
  }
  std::printf("%s tier=%d rect=(%d,%d,%d,%d)\n",name,tier,x,y,w,h);
  if(x!=16||y!=724||w!=448||h!=60) ++errors;
 }
 const int x0=16,w=448,h=tenorchrome::tabHeight(),y0=tenorchrome::touchBarTop(800);
'''
code += size_constants + '\n' + placement
code += r'''
 const int expected[]={66,153,240,326,413};
 for(int i=0;i<5;++i) {
   const int count=5;
   // CENTRE_EXPRESSION
   if(cx!=expected[i] || y0+h/2!=754 || h-2*tenorchrome::BAR_TAB_INSET!=48 || PILL_ICON_W!=84) ++errors;
 }
}
for(int tier=0;tier<3;++tier) {
 SETTINGS.uiTextSize=tier;
 if(tenorchrome::footBackTop(480)!=404 || tenorchrome::touchBarTop(480)!=404) ++errors;
}
std::printf("%s: %d screens x 3 sizes, Home centres, 84x48 selection, landscape, %d failures\n",errors?"RED":"GREEN",int(sizeof(names)/sizeof(*names)),errors);
return errors?1:0;
}
'''
code = code.replace('// CENTRE_EXPRESSION', centre_expression)
(a.work / 'geometry.cpp').write_text(code)
subprocess.run(['c++','-std=c++20','-DFREEINK_DEVICE_X4PRO=1','-I',str(a.work),'-I',str(ROOT/'test/button_symbols/stubs'),'-I',str(ROOT/'src'),'-I',str(ROOT/'src/components'),str(a.work/'geometry.cpp'),'-o',str(a.work/'geometry')],check=True)
result = subprocess.run([str(a.work/'geometry')])
if result.returncode == 0 and shared:
    assert 'touchTabBarRect(' not in u[:u.index('void UiTabListActivity::veThanhTheTenor')], 'duplicate rect policy'
    assert 'tenorchrome::touchTabBarRect(' in function(u, 'void UiTabListActivity::buildTabBar(')
    assert 'touchTabBarRect(' in function(c, 'void tenorchrome::drawFootBar(')
    assert 'touchTabBarRect(' in function(h, 'inline ReaderToolRect readerToolRect(')
(a.work/'screens.json').write_text(json.dumps(names,indent=2)+'\n')
raise SystemExit(result.returncode)
