"""Run existing SDK reader routing plus the production Ugly paint boundary under ASAN/UBSAN."""
from pathlib import Path
import argparse
import re
p = argparse.ArgumentParser(); p.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[2]); p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
s = (a.repo / 'src/activities/reader/ReaderToolbarUi.cpp').read_text()
assert 'void ReaderToolbarUi::paintUgly' in s, 'RED: reader toolbar and leaf layers have no Ugly skin'
assert 'paintUglyMenu' in (a.repo / 'src/activities/reader/EpubReaderMenuActivity.cpp').read_text(), 'RED: classic reader menu has no Ugly skin'
driver = (a.repo / 'test/x4_reader_menu/layout.py').read_text()
driver = driver.replace('#include <vector>', '#include <vector>\n#include <array>')
driver = driver.replace('class GfxRenderer {};', r'''
class GfxRenderer { public:
 mutable std::array<int,4> clip{0,0,480,800}; mutable int pen=0;
 auto getClipRect() const { return clip; }
 void setClipRect(int x,int y,int w,int h) const { clip={x,y,w,h}; }
 void fillRect(int,int,int,int,bool) const { ++pen; }
};
namespace ugly {
 enum class Size {S22}; enum class Circle {Row,Object}; enum class Mark {Left,Right,Back};
 struct Box {int x0,y0,x1,y1;};
 int width(const GfxRenderer&,Size,const char* s){return strlen(s)*10;}
 int ascent(Size){return 22;}
 std::string fit(const GfxRenderer&,Size,const std::string& s,int w){return s.substr(0,std::max(0,w)/10);}
 void text(const GfxRenderer& r,Size,int,int,const char*){++r.pen;}
 void line(const GfxRenderer& r,int,int,int,int,int,int){++r.pen;}
 void circle(const GfxRenderer& r,Circle,Box,int,int,int){++r.pen;}
 void mark(const GfxRenderer& r,Mark,int,int){++r.pen;}
 void tick(const GfxRenderer& r,int,int){++r.pen;}
}
''')
driver = driver.replace('UiAppHost(r) {}', 'UiAppHost(r),renderer_(&r) {}')
chrome = (a.repo / 'src/components/TenorMenuChrome.cpp').read_text()
chrome_names = re.search(r'static constexpr StrId names\[\].*?;', chrome).group(0)
chrome_lookup = re.search(r'(?:tr|I18N\.get)\(names\[i\]\)', chrome).group(0)
chrome_boundary = 'void chromeI18nBoundary(int i) {' + chrome_names + '(void)' + chrome_lookup + ';}\n'
driver = driver.replace("void ReaderToolbarUi::paintUgly() {}", "\'\'\' + s[s.index(\'void readerugly::text\'):] + chrome_boundary + r\'\'\'")
driver = driver.replace('for(int tier=0;tier<3;++tier) {', 'for(bool ugly:{false,true}) { shell::enabled=ugly; for(int tier=0;tier<3;++tier) {')
driver = driver.replace('  puts("GREEN X4', '  } assert(renderer.pen>0); assert((renderer.clip==std::array<int,4>{0,0,480,800}));\n  puts("GREEN Ugly + Cross X4')
# The generator uses its own parsed arguments, including this script's output and repo.
exec(compile(driver, str(a.repo / 'test/x4_reader_menu/layout.py'), 'exec'), {'__name__': '__main__', '__file__': str(a.repo / 'test/x4_reader_menu/layout.py'), 's': s, 'chrome_boundary': chrome_boundary})
