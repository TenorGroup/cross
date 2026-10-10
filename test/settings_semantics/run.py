"""Check the production choice policy against catalog semantics."""
import argparse
import re
import subprocess
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
a.output.mkdir(parents=True, exist_ok=True)
header = (root / 'src/components/SettingsChoiceStyle.h').read_text()
catalog = (root / 'src/SettingsList.h').read_text()
rows = []
for m in re.finditer(r'SettingInfo::(Toggle|Enum|StaticEnum|DynamicEnum)\(\s*StrId::(STR_\w+)', catalog):
    start = catalog.index('(', m.start())
    end, depth = start + 1, 1
    while depth:
        depth += (catalog[end] == '(') - (catalog[end] == ')')
        end += 1
    text = catalog[start:end]
    kind, name = m.groups()
    values = re.findall(r'StrId::(STR_\w+)', text)[1:]
    if kind == 'Toggle':
        count, onoff = 2, True
    elif kind == 'Enum' and '{' in text:
        values = re.findall(r'StrId::(STR_\w+)', text[text.index('{'):text.index('}')])
        count, onoff = len(values), set(values) == {'STR_STATE_OFF', 'STR_STATE_ON'} and len(values) == 2
    else:
        continue
    rows.append((name, count, onoff))
rows += [('STR_STATUS_CORNERS', 2, False), ('STR_FONT_FAMILY', 2, False),
         ('STR_CLOCK_FORMAT', 2, False), ('STR_CLOCK_AUTO_TIMEZONE', 2, True),
         ('STR_DOCUMENT_MATCHING', 2, False), ('STR_SEND_METADATA', 2, True), ('STR_SYNC_BEHAVIOR', 2, False)]
modern = 'const bool onOff' in header
checks = []
for name, count, onoff in rows:
    for touch in [False, True]:
        expected = 'Inline' if onoff else 'None' if count < 2 else 'Page' if count >= 7 else 'Popup'
        args = f'{count}, {str(touch).lower()}' + (f', {str(onoff).lower()}' if modern else '')
        checks.append(f'if(settingsChoiceStyle({args}) != SettingsChoiceStyle::{expected}) {{printf("RED {name} touch={touch} expected={expected}\\n"); ++bad;}}')
for touch in [False, True]:
    for count in range(0, 20):
        expected = 'None' if count < 2 else 'Page' if count >= 7 else 'Popup'
        args = f'{count}, {str(touch).lower()}'
        checks.append(f'if(settingsChoiceStyle({args}) != SettingsChoiceStyle::{expected}) ++bad;')
cpp = '#include <cstdio>\n#include "components/SettingsChoiceStyle.h"\nint main(){int bad=0;\n' + '\n'.join(checks) + f'\nprintf("%d failures, {len(checks)} checks, {len(rows)} catalog rows\\n",bad);return bad?1:0;}}\n'
path = a.output / 'policy.cpp'
path.write_text(cpp)
subprocess.run(['c++', '-std=c++20', '-I'+str(root/'src'),str(path),'-o',str(a.output/'policy')],check=True)
raise SystemExit(subprocess.run([str(a.output/'policy')]).returncode)
