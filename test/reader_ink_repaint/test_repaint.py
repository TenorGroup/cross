import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]
EPUB = (REPO / "src/activities/reader/EpubReaderActivity.cpp").read_text()
PREVIEW = (REPO / "src/activities/settings/TextSettingsPreview.cpp").read_text()


def extract(text, marker):
    start = text.index(marker)
    opening = text.index("{", start)
    depth = 1
    for position in range(opening + 1, len(text)):
        if text[position] == "{":
            depth += 1
        elif text[position] == "}":
            depth -= 1
            if depth == 0:
                return text[start:opening].strip(), text[opening + 1:position]
    raise AssertionError(marker)


SETTINGS = r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
struct StatusBar { bool slotsEnabled=true; bool showsTitle() const { return true; } bool textLaneVisible(bool) const { return true; } };
struct CrossPointSettings {
 uint8_t fontFamily=0,fontPointSize=16,lineSpacing=0,screenMargin=0,paragraphAlignment=0,extraParagraphSpacing=0,
 paragraphIndent=0,dropCapMode=0,hyphenationEnabled=0,embeddedStyle=0,textAntiAliasing=0,readerInkWeight=0,
 letterSpacing=0,wordSpacing=0,readerStatusBarMode=0,globalStatusBarMode=0;
 char sdFontFamilyName[64]="Geist";
 int getReaderFontId() const { return 7; }
 float getReaderLineCompression() const { return 1.0f; }
 StatusBar statusBarSpec() const { return {}; }
};
CrossPointSettings SETTINGS;
struct RenderLock { RenderLock() {} };
'''


def run(program):
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / "repaint.cpp"
        binary = Path(directory) / "repaint"
        source.write_text(program)
        subprocess.run([os.environ.get("CXX", "c++"), "-std=c++20", "-I" + str(REPO / "src"),
                        "-I" + str(REPO / "src/activities/settings"), str(source), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


class Repaint(unittest.TestCase):
    def test_settings_pop_preserves_page_and_offset(self):
        _, snapshot = extract(EPUB, "struct AnhChupChu")
        start = EPUB.index("std::make_unique<TextSettingsActivity>")
        _, callback = extract(EPUB[start:], "[this, truoc")
        prefix = r'''
class GfxRenderer {};
namespace readerInk { void apply(GfxRenderer&) {} }
struct ActivityResult {};
struct EpubReaderActivity {
 GfxRenderer renderer;
 int page=31,offset=917,section=1,reflows=0,updates=0,discards=0;
 void danLaiTrang() { ++reflows; page=0; offset=0; section=0; }
 void discardOverlayPage() { ++discards; }
 void requestUpdate() { ++updates; }
 void pop(const AnhChupChu& truoc,uint8_t inkTruoc,uint8_t aaTruoc) {
 @CALLBACK@
 }
};
int main() {
 EpubReaderActivity reader;
 for(int level=0;level<6;++level) for(int aa=0;aa<2;++aa) {
  const auto snapshot=AnhChupChu::chup();
  auto inkTruoc=SETTINGS.readerInkWeight,aaTruoc=SETTINGS.textAntiAliasing;
  SETTINGS.readerInkWeight=level;SETTINGS.textAntiAliasing=aa;
  reader.pop(snapshot,inkTruoc,aaTruoc);
  assert(reader.reflows==0 && reader.section==1 && reader.page==31 && reader.offset==917);
  assert(snapshot==AnhChupChu::chup());
 }
 assert(reader.discards>0 && reader.updates>0);
}
'''
        run(SETTINGS + "struct AnhChupChu {" + snapshot + "};\n" + prefix.replace("@CALLBACK@", callback))

    def test_toolbar_pick_and_close_discard_the_old_snapshot(self):
        signature, apply = extract(EPUB, "void EpubReaderActivity::applyReaderTextSettingsLocked")
        _, invalidate = extract(EPUB, "void EpubReaderActivity::invalidateTextSettingsLocked")
        _, close = extract(EPUB, "void EpubReaderActivity::closeOverlayToPage")
        declaration = signature.replace("void EpubReaderActivity::", "void ")
        program = r'''
#include "ReaderInkWeight.h"
struct GfxRenderer { int restores=0,displays=0; void restoreBwBuffer(bool) { ++restores; } void displayBuffer(int) { ++displays; } };
namespace readerInk { void apply(GfxRenderer&) {} }
struct FontSystem { int loads=0; void ensureLoaded(GfxRenderer&) { ++loads; } } sdFontSystem;
struct HalDisplay { static constexpr int FAST_REFRESH=0; };
struct Empty { void reset() {} void dismiss() {} void resetHomeButtonInput() {} };
struct EpubReaderActivity {
 GfxRenderer renderer;
 int page=31,offset=917,section=1,reflows=0,updates=0,discards=0;
 bool textSettingsDirty=false,overlayPageStored=true,bwUnderSheet=false;
 std::atomic<int> textCloseFrame{0};
 enum class Overlay { None }; Overlay overlay;
 Empty overlayPopup,toolbarUi,mappedInput;
 bool xteinkClassPanel() { return false; }
 void settleOverlayRefresh() {}
 void panelClosedLocked(bool,bool) {}
 void danLaiTrang() { ++reflows; page=0; offset=0; section=0; }
 void discardOverlayPage() { ++discards; overlayPageStored=false; }
 void requestUpdate() { ++updates; }
 void invalidateTextSettingsLocked() { @INVALIDATE@ }
 @DECLARATION@ { @APPLY@ }
 void closeOverlayToPage() { @CLOSE@ }
};
template<typename Reader> void pick(Reader& reader,const char* key) {
 if constexpr(requires { reader.applyReaderTextSettingsLocked(key); }) reader.applyReaderTextSettingsLocked(key);
 else reader.applyReaderTextSettingsLocked();
}
int main() {
 EpubReaderActivity reader;
 for(const char* key : {"readerInkWeight","textAntiAliasing"}) {
  reader.overlayPageStored=true;
  pick(reader,key);
  assert(reader.reflows==0 && reader.section==1 && reader.page==31 && reader.offset==917);
  assert(!reader.overlayPageStored && reader.updates>0 && reader.textSettingsDirty);
  reader.closeOverlayToPage();
  assert(reader.renderer.restores==0 && reader.renderer.displays==0);
 }
 pick(reader,"letterSpacing");assert(reader.reflows==1);
}
'''
        for key, value in {"INVALIDATE": invalidate, "APPLY": apply, "CLOSE": close, "DECLARATION": declaration}.items():
            program = program.replace("@" + key + "@", value)
        run(SETTINGS + program)
        choose = extract(EPUB, "void EpubReaderActivity::chooseTextValue")[1]
        cycle = extract(EPUB, "void EpubReaderActivity::cycleTextRow")[1]
        self.assertRegex(choose, r"applyReaderTextSettingsLocked\(info->key\)")
        self.assertRegex(cycle, r"applyReaderTextSettingsLocked\(key\)")

    def test_preview_keeps_lines_and_prewarms_when_only_ink_changes(self):
        _, preview = extract(PREVIEW, "void renderPreview(")
        program = r'''
#include "TextSettingsPreview.h"
namespace EpdFontFamily { static constexpr int REGULAR=0; }
namespace readerSpacing { int paragraphGap(int,int) { return 0; } static constexpr int DROP_CAP_OFF=0; }
enum class StrId { STR_FONT_PREVIEW_TEXT };
static constexpr int UI_10_FONT_ID=10,STR_PREVIEW=0;
const char* tr(int) { return "Preview"; }
std::string utf8ComposeNfc(const char* text) { return text; }
struct Internationalization { const char* get(StrId) const { return "sample"; } int getLanguage() const { return 0; } } I18N;
struct FontCacheManager {
 int clears=0,prewarms=0;
 void clearCache() { ++clears; }
 void prewarmCache(int,const char*,uint8_t) { ++prewarms; }
};
class GfxRenderer {
 public:
 mutable FontCacheManager manager;
 int getScreenWidth() const { return 600; }
 int getTextHeight(int) const { return 30; }
 int getTextWidth(int,const char*) const { return 100; }
 int getLineHeight(int,float) const { return 32; }
 std::string truncatedText(int,const char* text,int,int) const { return text; }
 void drawText(int,int,int,const char*) const {}
 FontCacheManager* getFontCacheManager() const { return &manager; }
};
class TextBlock {
 public:
 int getDropCapHeight() const { return 0; }
 void render(const GfxRenderer&,int,int,int) const {}
};
namespace textsettings {
PreviewLayout::PreviewLayout() = default;
PreviewLayout::~PreviewLayout() = default;
int relayouts=0;
uint32_t previewFontFamilyIdentity() { return 42; }
void relayout(PreviewLayout& layout,const GfxRenderer&,int,int) {
 ++relayouts; layout.lines.clear(); layout.lines.push_back(std::make_unique<TextBlock>());layout.firstParagraphLines=1;
}
void renderPreview(const GfxRenderer& renderer,PreviewLayout& layout,int previewPadding,int labelGap,int top,int height,
                   const char* familyName,const char* sizeName) { @PREVIEW@ }
}
int main() {
 GfxRenderer renderer;textsettings::PreviewLayout layout;
 textsettings::renderPreview(renderer,layout,8,8,0,400,"Geist","16");
 auto* line=layout.lines[0].get();
 for(int level=0;level<6;++level) for(int aa=0;aa<2;++aa) {
  const bool changed=SETTINGS.readerInkWeight!=level || SETTINGS.textAntiAliasing!=aa;
  const int before=renderer.manager.prewarms;
  SETTINGS.readerInkWeight=level;SETTINGS.textAntiAliasing=aa;
  textsettings::renderPreview(renderer,layout,8,8,0,400,"Geist","16");
  assert(textsettings::relayouts==1 && layout.lines[0].get()==line);
  assert(renderer.manager.prewarms==before+(changed?1:0));
  textsettings::renderPreview(renderer,layout,8,8,0,400,"Geist","16");
  assert(renderer.manager.prewarms==before+(changed?1:0));
 }
}
'''
        run(SETTINGS + program.replace("@PREVIEW@", preview))


if __name__ == "__main__":
    unittest.main()
