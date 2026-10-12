"""Rebuild the X4 Pro sleep artwork (480x800, four levels) from the X3 sleep page.

The page is scripts/brand-art/x3/muc-dang.html itself, moved onto the X4 Pro panel the way the
X4 Pro boot page was moved from the X3 boot page: sizes and x by 480/528, y by 800/792, the
lockup on the column of the X4 Pro boot lockup (x4pro-boot.html). The pipeline is the X3 sleep
one (x3/render.py): background, grain and the large plus through the firmware Bayer dither,
lockup and mark from a scale 4 shot through tenor/ink.

Output: x4pro-sleep.png (four levels) and output/ (scratch, ignored by git).
"""
import hashlib
import sys
from pathlib import Path

import numpy as np
from PIL import Image
from scipy.ndimage import binary_dilation

HERE = Path(__file__).resolve().parent
X3 = HERE.parent / 'x3'
sys.path.insert(0, str(X3))
import render as x3  # noqa: E402
from tenor_ink import tenor_ink4  # noqa: E402

W, H = 480, 800
SX, SY = W / 528, H / 792


def page():
    html = x3.HAT_DAU.sub(r'\1\2', (X3 / 'muc-dang.html').read_text())
    swaps = [
        ('body { width:528px; height:792px;', f'body {{ width:{W}px; height:{H}px;'),
        ('.col { position:absolute; left:61px; width:406px; }',
         '.col { position:absolute; left:50%; width:max-content; transform:translateX(-50%); margin-left:-12px; }'),
        ('font-size:80px;', f'font-size:{80 * SX:.3f}px;'),
        ('.sym { position:absolute; left:116px; top:382px; width:400px; height:260px; }',
         f'.sym {{ position:absolute; left:{116 * SX:.2f}px; top:{382 * SY:.2f}px; '
         f'width:{400 * SX:.2f}px; height:{260 * SY:.2f}px; }}'),
        ('style="top:104px;"', f'style="top:{round(104 * SY)}px;"'),
        ('vung-cam=[[46,86,482,190]]', f'vung-cam=[[{round(46 * SX)},{round(86 * SY)},{round(482 * SX)},{round(190 * SY)}]]'),
    ]
    for old, new in swaps:
        assert html.count(old) == 1, old
        html = html.replace(old, new)
    return html


def big_plus(px):
    """x3.hat_bu at the X4 Pro scale: its tone measured along its own body, half a level off."""
    cx, cy, nua, day = 104.0 * SX, 476.0 * SY, 196.0 * SX, 84.0 * SX
    tren, duoi = cy - nua - day / 2, cy + nua + day / 2
    xs = max(0, min(W - 1, int(cx + nua * 0.5)))
    stops = []
    for k in range(9):
        t = k / 8
        tone = int(px[xs, int(max(0, min(H - 1, tren + (duoi - tren) * t)))])
        tone = max(0, min(255, tone - x3.BUOC_BU if tone >= 128 else tone + x3.BUOC_BU))
        stops.append(f'<stop offset="{t*100:.0f}%" stop-color="#{tone:02X}{tone:02X}{tone:02X}"/>')
    d = f"M{cx-nua:.1f} {cy:.1f}H{cx+nua:.1f}M{cx:.1f} {cy-nua:.1f}V{cy+nua:.1f}"
    return ('<defs><linearGradient id="bu" gradientUnits="userSpaceOnUse" '
            f'x1="0" y1="{tren:.1f}" x2="0" y2="{duoi:.1f}">' + "".join(stops) +
            '</linearGradient></defs>'
            f'<path d="{d}" stroke="url(#bu)" stroke-width="{day:.0f}" stroke-linecap="square"'
            f' transform="rotate(-15 {cx:.1f} {cy:.1f})"/>')


def sleep():
    # The X3 pipeline, run at this panel's size (x3.render reads these module values).
    x3.W, x3.H, x3.OUT = W, H, HERE / 'output'
    x3.O_MARK = [150 * SX, 402 * SY, 486 * SX, 636 * SY]
    x3.hat_bu = big_plus
    x3.OUT.mkdir(exist_ok=True)
    clean = page()
    nen = x3.chup(clean, 'muc-dang-nen')
    grain = x3.rai_hat(nen, x3.doc_vung_cam(clean)).replace(
        'width="528" height="792" viewBox="0 0 528 792"', f'width="{W}" height="{H}" viewBox="0 0 {W} {H}"')
    full = clean.replace('  <!-- hat:cuoi -->', grain + '  <!-- hat:cuoi -->')
    (x3.OUT / 'muc-dang-full.html').write_text(full)
    bay = x3.rac_hat_panel(x3.chup(full, 'muc-dang-x1'))
    g4 = x3.chup(full, 'muc-dang-x4', 4).resize((W, H), Image.Resampling.BOX)
    ink = tenor_ink4(np.array(g4))
    chu = x3.HAT_DAU.sub(r'\1\2', full).replace('</style>', '.muc{display:none!important}</style>', 1)
    mask = binary_dilation(np.array(x3.chup(chu, 'muc-dang-chu')) < 250, iterations=1)
    return Image.fromarray(np.where(mask, ink, np.array(bay)).astype(np.uint8))


if __name__ == '__main__':
    if not Path(x3.CHROME).exists():
        sys.exit('Google Chrome is required to render.')
    image = sleep()
    assert image.size == (W, H) and set(np.unique(np.array(image))) <= set(x3.MUC)
    target = HERE / 'x4pro-sleep.png'
    image.save(target)
    print(target.name, hashlib.sha256(target.read_bytes()).hexdigest())
