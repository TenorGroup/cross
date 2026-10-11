"""Exercise the status row's production wrapping and value slot with builtin font metrics."""
import argparse
import json
from pathlib import Path
import re
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--output', type=Path, required=True)
p.add_argument('--no-wrap', action='store_true', help='Inject the old single-line layout for the regression check')
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
a.output.mkdir(parents=True, exist_ok=True)
source = (root / 'src/activities/settings/SettingsActivity.cpp').read_text()
body = source[source.index('void SettingsActivity::buildScreen('):]
recipe = '\n'.join(re.findall(r'  props\.(?:labelText(?:\.maxLines)?|valueInset|balanceWrappedLabelWithValue)\s*=[^;]+;', body))
if a.no_wrap:
    recipe += '\nprops.labelText.maxLines = 1;'
# The framed touch row reserves the popup marker independently of its wrapping.
list_source = (root / 'src/activities/UiListActivity.cpp').read_text()
frame = list_source[list_source.index('void UiListActivity::frameRows('):]
frame = frame[:frame.index('\n}\n')]
assert 'props.chevronColumn = true;' in frame
inset = re.search(r'constexpr int FOOT_BACK_X = (\d+);', (root / 'src/components/TenorMenuChrome.h').read_text())[1]
side = re.search(r'props.sidePadding = (\d+);', frame)[1]
board = (root / 'freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h').read_text()
profile = board[board.index('constexpr BoardProfile XTEINK_X4_PRO = {'):]
profile = profile[:profile.index('true};')]
bezel = re.search(r'\{(\d+), (\d+), (\d+), (\d+)\},\s*$', profile)[0]
safe_top, safe_right, safe_bottom, safe_left = map(int, re.findall(r'\d+', bezel))
cases = []
for language in ('vietnamese', 'english', 'chinese'):
    translation = (root / f'lib/I18n/translations/{language}.yaml').read_text()
    def text(key):
        return re.search(r'^' + key + r': "(.*?)"', translation, re.M)[1]
    for key in ('STR_BATTERY_LEFT_CLOCK_RIGHT', 'STR_CLOCK_LEFT_BATTERY_RIGHT'):
        cases.append('check(' + json.dumps(language) + ',' + json.dumps(text('STR_STATUS_CORNERS'), ensure_ascii=False) + ',' + json.dumps(text(key), ensure_ascii=False) + ');')
cpp = r'''
#include "test/ui_layout/RecordingTarget.h"
#include <EpdFont.h>
#include <builtinFonts/geist_10_regular.h>
#include <builtinFonts/geist_12_regular.h>
#include <builtinFonts/geist_14_regular.h>
#include <builtinFonts/geist_16_regular.h>
#include <cctype>
struct FontTarget : Target {
  EpdFont body, small;
  std::vector<fui::Rect> blocks;
  explicit FontTarget(int tier) : Target(tier),
      body(tier==0?&geist_12_regular:tier==1?&geist_14_regular:&geist_16_regular),
      small(tier==0?&geist_10_regular:tier==1?&geist_12_regular:&geist_14_regular) {}
  fui::Size measureText(fui::FontId font, const char* text, fui::TextStyle) const override {
    int w,h; (font==1?body:small).getTextDimensions(text,&w,&h);
    return {static_cast<int16_t>(w),lineHeight(font)};
  }
  static std::string compact(const char* text) {
    std::string result;
    for (;*text;++text) if (!std::isspace(static_cast<unsigned char>(*text))) result+=*text;
    return result;
  }
  void text(fui::Rect rect, const char* text, fui::TextStyle style) override {
    blocks.push_back(rect);
    std::string rendered;
    fui::layoutText(*this,rect,text,style,[&](const char* line,fui::Rect run) {
      assert(measureText(style.font,line,style).width<=rect.width);
      assert(run.x>=rect.x && run.right()<=rect.right());
      assert(run.y>=rect.y && run.bottom()<=rect.bottom());
      runs.push_back(run);
      rendered+=compact(line);
    });
    assert(rendered==compact(text)); // Every word survives the production layout.
  }
};
namespace tenorchrome { bool x3ScreenLayoutEnabled() { return false; } }
struct ScreenBoundary {
  fui::ThemeTokens tokens;
  ScreenBoundary() { tokens.bodyText.font=1; tokens.smallText.font=0; }
  const fui::ThemeTokens& theme() const { return tokens; }
} screen;
const fui::TextStyle& uiMenuLabelText(const fui::ThemeTokens& theme) { return theme.bodyText; }
void check(const char* language,const char* label,const char* value) {
  for (int tier=0;tier<3;++tier) {
    FontTarget target(tier);
    fui::ListProps props;
    RECIPE
    props.valueText=screen.theme().smallText;
    props.sidePadding=SIDE;
    props.chevronColumn=true;
    props.scrollIndicator=false;
    props.rowInset=INSET;
    props.rowPaddingY=8;
    props.rowHeight=56;
    fui::ListItem item;item.label=label;item.value=value;
    props.items=&item;props.count=1;
    const int rowWidth=480-SAFE_LEFT-SAFE_RIGHT-2*props.rowInset;
    const auto measured=fui::measureListRow(target,nullptr,rowWidth,props,item);
    fui::DeviceContext device;device.width=480;device.height=800;device.hasTouch=true;
    fui::InputSnapshot input;fui::InteractionBuffer<2> hits;
    fui::Frame<2> frame(target,device,input,hits);
    fui::list(frame,{SAFE_LEFT,0,480-SAFE_LEFT-SAFE_RIGHT,200},props);
    assert(target.blocks.size()==2);
    const auto v=target.blocks[0],l=target.blocks[1];
    const int gap=v.x-l.right();
    assert(gap>=props.textGap);
    int valueLeft=480,labelRight=0;
    for (size_t i=0;i<target.runs.size();++i) {
      if (i<static_cast<size_t>(measured.valueLines)) valueLeft=std::min(valueLeft,int(target.runs[i].x));
      else labelRight=std::max(labelRight,int(target.runs[i].right()));
    }
    assert(valueLeft-labelRight>=props.textGap);
    // Popup ink occupies row.right()-31 through row.right()-19.
    const int markerLeft=480-SAFE_RIGHT-props.rowInset-31;
    assert(v.right()<markerLeft);
    printf("GREEN %s tier=%d row=%d label=%d/%d value=%d/%d gap=%d marker_gap=%d\n",
           language,tier,rowWidth,measured.labelWidth,measured.labelLines,
           measured.valueWidth,measured.valueLines,valueLeft-labelRight,markerLeft-v.right());
  }
}
int main() { CASES }
'''.replace('RECIPE', recipe).replace('SIDE', side).replace('INSET', inset).replace('CASES', '\n'.join(cases)).replace('SAFE_LEFT', str(safe_left)).replace('SAFE_RIGHT', str(safe_right))
path = a.output / 'label.cpp'
path.write_text(cpp)
exe = a.output / 'label'
sdk = root / 'freeink-sdk/libs/ui/FreeInkUI'
subprocess.run(['c++', '-std=c++20', '-UNDEBUG', '-I'+str(root), '-I'+str(root/'src'),
                '-I'+str(root/'lib/EpdFont'), '-I'+str(root/'lib/Utf8'), '-I'+str(sdk/'include'),
                str(path), str(root/'lib/EpdFont/EpdFont.cpp'), str(root/'lib/Utf8/Utf8.cpp'),
                str(sdk/'src/FreeInkUI.cpp'), '-o', str(exe)], check=True)
raise SystemExit(subprocess.run([str(exe)]).returncode)
