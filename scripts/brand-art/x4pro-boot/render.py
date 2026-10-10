"""Rebuild V3 from the approved grayscale input and embedded-font HTML masks."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

import numpy as np
from PIL import Image
from scipy.ndimage import label, center_of_mass

from quantize import quantize

HERE = Path(__file__).resolve().parent
OUT = HERE / 'output'
CHROME = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'


def capture(html, name):
    path = OUT / (name + '.html')
    target = OUT / (name + '.png')
    path.write_text(html)
    target.unlink(missing_ok=True)
    subprocess.run([CHROME, '--headless=new', '--disable-gpu', '--hide-scrollbars',
                    '--no-first-run', '--no-default-browser-check',
                    '--user-data-dir=' + str(OUT / 'chrome'),
                    '--force-device-scale-factor=1', '--window-size=480,800',
                    '--default-background-color=FFFFFFFF', '--screenshot=' + str(target),
                    path.as_uri()], check=True, stdout=subprocess.DEVNULL,
                   stderr=subprocess.DEVNULL, timeout=30)
    image = np.array(Image.open(target).convert('L'))
    assert image.shape == (800, 480)
    return image


def main():
    variant = sys.argv[1] if len(sys.argv) > 1 else 'V3'
    # Check the optional dependency before starting the renderer.
    if variant == 'V4' and not os.environ.get('TENOR_DITHER_ROOT'):
        quantize(np.zeros((1, 1), dtype=np.uint8), variant)
    OUT.mkdir(exist_ok=True)
    html = (HERE / 'x4pro-boot.html').read_text()
    gray = np.array(Image.open(HERE / 'x4pro-boot-gray.png').convert('L'))
    assert np.array_equal(capture(html, 'gray-render'), gray), 'HTML grayscale differs from approved input'
    masks = {}
    for part in ('all', 'ns', 'sl'):
        rules = 'body {background:#000} .muc,.hat-nen,.symclip,.trang-thai {display:none} .lockup,.lockup .ns,.lockup .sl {color:#000}'
        rules += '.lockup,.lockup .ns,.lockup .sl {color:#fff}' if part == 'all' else '.lockup .' + part + ' {color:#fff}'
        masks[part] = capture(html + '<style>' + rules + '</style>', 'mask-' + part) > 127
    mark = capture(html + '<style>body {background:#000} .muc,.hat-nen,.lockup,.trang-thai {display:none} .sym text,.sym tspan {fill:#fff !important}</style>', 'mask-mark') > 127
    inputs = {}
    if variant == 'V4':
        import re
        background_html = re.sub(r'(<!-- hat:dau[^>]*-->\n).*?(  <!-- hat:cuoi -->)', r'\1\2', html, flags=re.S)
        inputs = dict(background=capture(background_html, 'background'), lockup_mask=masks['all'], mark_mask=mark)
    panel = quantize(gray, variant, **inputs)
    labels, count = label(mark)
    assert count == 3
    roles = sorted(range(1, count + 1), key=lambda i: center_of_mass(labels == i)[1])
    for idx, value in zip(roles, (85, 170, 0)):
        panel[labels == idx] = value
    panel[masks['all']] = 255
    panel[masks['ns'] | masks['sl']] = 170
    assert set(np.unique(panel)) == {0, 85, 170, 255}
    target = OUT / 'x4pro-boot.png'
    Image.fromarray(panel).save(target)
    provenance = dict(selected=variant, inputs={name: hashlib.sha256((HERE / name).read_bytes()).hexdigest()
                     for name in ('x4pro-boot.html', 'x4pro-boot-gray.png', 'render.py', 'quantize.py')},
                     output_sha256=hashlib.sha256(target.read_bytes()).hexdigest(),
                     brand_codes=dict(mark_t=1, mark_slash=2, mark_plus=0, tenor=2, slash=2, cross=3))
    (OUT / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
    print('GREEN', variant, provenance['output_sha256'])


if __name__ == '__main__':
    main()
