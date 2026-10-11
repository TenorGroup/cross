import subprocess


def bitmap_build_args(root, work):
    font_include = root / 'freeink-sdk/libs/font/FreeInkFont/include'
    objects = []
    for source in (root / 'lib/uzlib/src/tinflate.c',
                   root / 'freeink-sdk/libs/font/FreeInkFont/src/FontAlloc.c'):
        target = work / (source.stem + '.o')
        subprocess.run(['cc', '-I', str(root / 'lib/uzlib/src'), '-I', str(font_include),
                        '-c', str(source), '-o', str(target)], check=True)
        objects.append(str(target))
    return ['-I', str(root / 'test/fixed_menu_chrome'), '-I', str(root / 'test/ui_tiers/stubs'),
            '-I', str(font_include), '-I', str(root / 'lib/InflateReader'), '-I', str(root / 'lib/Memory'),
            '-I', str(root / 'lib/uzlib/src'), str(root / 'lib/EpdFont/FontDecompressor.cpp'),
            str(root / 'lib/InflateReader/InflateReader.cpp'), *objects, '-lz']


def extend_path(fixture, chrome, function, full_painter, root):
    includes = '''
#include "components/X3NavigationPath.h"
#include "components/icons/tenorHomeTabIcons.h"
#include "components/icons/tenorReaderTabIcons.h"
#include "components/icons/readerToolbarTabIcons.h"
#include "components/icons/tenorSettingsIcons.h"
'''
    fixture = includes + fixture
    declarations = '''
namespace tenorchrome {
// The hand's path (tenor/ugly) is written by UglyInk in the firmware; the harness draws none.
inline void drawHeader(const GfxRenderer&, const char*, const char* = "", const char* = nullptr) {}
void drawPillRing(const GfxRenderer&, int, int, int, int, int, bool);
void drawPillLabel(const GfxRenderer&, int, const char*, int, int, int, int);
void drawBarIcon(const GfxRenderer&, const uint8_t*, int, int, int, int, bool);
void drawX3NavigationPath(const GfxRenderer&, const std::vector<X3PathNode>&, bool);
void drawX3ActivityPath(const GfxRenderer&, const char*);
}
struct PathActivityBoundary {
  bool file = false;
  std::vector<tenorchrome::X3PathNode> x3NavigationPath(const char* title) const {
    return {{file ? tenorchrome::X3PathIcon::File : tenorchrome::X3PathIcon::Settings, ""},
            {tenorchrome::X3PathIcon::More, title ? title : ""}};
  }
  bool x3NavigationIsFilePath() const { return file; }
} activityManager;
'''
    if not full_painter:
        if 'tenorchrome::drawPillLabel(' in chrome:
            declarations += function(chrome, 'tenorchrome::drawPillLabel(') + '\n'
        declarations += '''
namespace tenorchrome {
constexpr int BAR_TAB_INSET = 6;
struct PathPillLayout { std::string text; int width; };
PathPillLayout layoutPathPill(const GfxRenderer&, int, const char*, int, int, int);
void drawPathPill(const GfxRenderer&, int, const char*, int, int, int, int, int, int, bool);
}
'''
        declarations += function(chrome, 'tenorchrome::layoutPathPill(') + '\n'
        declarations += function(chrome, 'tenorchrome::drawPathPill(') + '\n'
        icon = function(chrome, 'tenorchrome::drawBarIcon(')
        icon = icon.replace('  if (shell::isUgly() && ugly::iconAsset(r, bits, {x, y, w, h}, ugly::iconFrameSeed())) return;\n', '')
        declarations += icon + '\n'
    return fixture + declarations + function(chrome, 'const freeink::Icon& x3PathIcon(') + '\n' + \
        function(chrome, 'tenorchrome::drawX3NavigationPath(') + '\n' + \
        function(chrome, 'tenorchrome::drawX3ActivityPath(') + '\n'
