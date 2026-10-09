import os
from pathlib import Path
import re
import sys

from PIL import Image


root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'scripts/icons'))
import sinh

reference = Path(os.environ.get('ICON57_REFERENCE_DIR', root / 'scripts/icons/icon57/png'))
expected = {path.stem for path in reference.glob('*.png')}
seen = set()
failures = []
for filename, size, stroke, items in sinh.SETS:
    if size != 40:
        continue
    text = (root / 'src/components/icons' / filename).read_text()
    for cname, shape, *own in items:
        suffix = 'chon' if cname.endswith('_bold') else 'xam'
        name = f'{shape}-{suffix}'
        if name not in expected:
            continue
        array = re.search(rf'icon_{cname}_40_bits\[\]\s*=\s*\{{(.*?)\}}', text, re.S)
        assert array, cname
        actual = bytes(int(value, 16) for value in re.findall(r'0x([0-9A-Fa-f]{2})', array[1]))
        approved = Image.open(reference / f'{name}.png')
        assert approved.mode == '1' and approved.size == (40, 40)
        if actual != approved.tobytes():
            failures.append(cname)
        seen.add(name)
assert not failures, failures
assert seen == expected and len(expected) == 42, sorted(expected - seen)
print('GREEN: 42 approved bitmaps match header bytes exactly')
