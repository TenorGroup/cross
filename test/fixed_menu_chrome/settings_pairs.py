from pathlib import Path
import argparse
import re
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--work', type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
recipes = []
for name in ('Settings', 'KOReaderSettings'):
    source = (root / f'src/activities/settings/{name}Activity.cpp').read_text()
    start = source.index(f'void {name}Activity::buildScreen(')
    assignments = re.findall(r'  props\.(?:labelText(?:\.maxLines)?|valueInset|balanceWrappedLabelWithValue)\s*=[^;]+;', source[start:])
    recipes.append('{ fui::ListProps props; ' + '\n'.join(assignments) + ' checkPairs(props); }')
unit = r'''
#include "test/ui_layout/RecordingTarget.h"
namespace tenorchrome { bool x3ScreenLayoutEnabled() { return true; } }
struct ScreenBoundary {
  fui::ThemeTokens tokens;
  ScreenBoundary() { tokens.bodyText.font = 1; tokens.smallText.font = 0; }
  const fui::ThemeTokens& theme() { return tokens; }
} screen;
const fui::TextStyle& uiMenuLabelText(const fui::ThemeTokens& theme) { return theme.bodyText; }
void checkPairs(fui::ListProps props) {
  assert(props.labelText.maxLines == 2);  // a label that does not fit goes to a second line
  assert(!props.balanceWrappedLabelWithValue);
  for (int tier = 0; tier < 3; ++tier) {
    Target target(tier);
    props.valueText = screen.theme().smallText;
    props.sidePadding = 14;
    for (const char* label : {"Cài đặt nút cạnh trên và dưới", "Cách đồng bộ", "A very long setting label that must be shortened"}) {
      for (const char* value : {"Đồng bộ thông minh", "Chuyển chương", "Smart synchronization"}) {
        fui::ListItem item;
        item.label = label;
        item.value = value;
        const auto measured = fui::measureListRow(target, nullptr, 480, props, item);
        const int fullValue = target.measureText(props.valueText.font, value, props.valueText).width;
        assert(measured.valueWidth == fullValue && measured.valueLines == 1);
        assert(measured.labelLines >= 1 && measured.labelLines <= 2);
        const int valueLeft = 480 - 2 * props.sidePadding - props.valueInset - fullValue;
        assert(valueLeft - measured.labelWidth >= props.textGap);
      }
    }
  }
}
int main() { RECIPES }
'''.replace('RECIPES', '\n'.join(recipes))
args.work.mkdir(parents=True, exist_ok=True)
path = args.work / 'pairs.cpp'
path.write_text(unit)
program = args.work / 'pairs'
sdk = root / 'freeink-sdk/libs/ui/FreeInkUI'
subprocess.run(['c++', '-std=c++20', '-UNDEBUG', '-I', str(root), '-I', str(root / 'src'),
                '-I', str(sdk / 'include'), str(path), str(sdk / 'src/FreeInkUI.cpp'), '-o', str(program)], check=True)
subprocess.run([str(program)], check=True)
print('GREEN: 2 settings recipes, 3 text sizes, wrapped or cut labels and full values separated by ListProps.textGap')
