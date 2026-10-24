#!/usr/bin/env python3
"""Every key a web page asks the shared helper for exists in all three catalogues, and the catalogues agree."""
import pathlib
import re
import sys

HTML = pathlib.Path(__file__).resolve().parents[2] / 'src' / 'network' / 'html'
js = (HTML / 'js' / 'WebI18n.js').read_text(encoding='utf-8')
tables = {}
for name, start in (('vi', '    vi: {'), ('en-AU', "    'en-AU': {"), ('zh-Hans', "    'zh-Hans': {")):
    i = js.index(start)
    tables[name] = set(re.findall(r"^      '([\w.]+)':", js[i:js.index('\n    }', i)], re.M))

def base(key):  # plural forms are looked up as key.one / key.other
    return re.sub(r'\.(one|other)$', '', key)

bad = []
union = {base(k) for t in tables.values() for k in t}
for name, keys in tables.items():
    for k in sorted(union - {base(k) for k in keys}):
        bad.append(f'{name} lacks {k}')

used = set()
sources = list(HTML.glob('*.html')) + list((HTML / 'shared').glob('*')) + list((HTML / 'js').glob('font-builder.js'))
for path in sources:
    text = path.read_text(encoding='utf-8')
    for m in re.finditer(r'data-i18n(?:-[a-z-]+)?="([\w.]+)"', text):
        used.add((m.group(1), path.name))
    for m in re.finditer(r"""(?:\.t|tPlural)\(\s*['"]([\w.]+)['"]""", text):
        used.add((m.group(1), path.name))
for key, page in sorted(used):
    if key.endswith('.'):  # dynamic suffix, checked at run time by TenorI18n.missing
        continue
    if base(key) not in union:
        bad.append(f'{page} uses unknown key {key}')
if bad:
    sys.exit('\n'.join(bad))
print(f'GREEN {len(union)} keys in 3 catalogues, {len(used)} page uses')
