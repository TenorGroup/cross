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
void drawPillRing(const GfxRenderer&, int, int, int, int, int, bool);
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
    legacy = (root / 'src/activities/X3LegacyPath.h').read_text()
    declarations += '\nnamespace uglylegacy {\n'
    for name in ('drawFormPath', 'drawSettingsPath', 'drawReaderPath'):
        declarations += function(legacy, 'inline void ' + name + '(') + '\n'
    declarations += '}\n'
    return fixture + declarations + function(chrome, 'const freeink::Icon& x3PathIcon(') + '\n' + \
        function(chrome, 'tenorchrome::drawX3NavigationPath(') + '\n' + \
        function(chrome, 'tenorchrome::drawX3ActivityPath(') + '\n'
