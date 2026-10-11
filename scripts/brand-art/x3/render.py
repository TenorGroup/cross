"""Rebuild the X3 boot and sleep artwork (528x792, four levels) from khoi-dong.html and muc-dang.html.

Boot: Chrome shot at scale 1, then the firmware Bayer dither (applyBayerDither4Level).
Sleep: background, grain and the large plus use the same Bayer path. The lockup and the
"t/+" mark come from a scale 4 shot, BOX-reduced to 528x792 and quantized with tenor/ink
(tenor_ink.py). The mask is every pixel darker than 250 in a shot that holds only the
lockup and mark, dilated by 1 pixel.

Output: bmp/<name>.bmp (8-bit gray BMP, four levels) and output/ (scratch, ignored by git).
"""
import hashlib
import json
import random
import re
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image
from scipy.ndimage import binary_dilation

from tenor_ink import tenor_ink4

HERE = Path(__file__).resolve().parent
OUT = HERE / 'output'
BMP = HERE / 'bmp'
CHROME = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'
W, H = 528, 792
MUC = [0, 85, 170, 255]
BAYER = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]
BOOT_BMP = '261011_tenor_branding_xteink-x3-cross-khoi-dong.bmp'
SLEEP_BMP = '261011_tenor_branding_xteink-x3-cross-muc-dang.bmp'

# Grain grid. One grain at most per 30 px cell, jittered inside the cell.
O = 30
TI_LE_CO_HAT = 0.74
HAT_NHO, HAT_LON = 3.5, 9.0
O_MARK = [150, 402, 486, 636]
# Grain sits exactly one panel level off the background it is drawn on.
BUOC_NHAT = 85
# The large plus is only half a level off its background.
BUOC_BU = 34
HAT_DAU = re.compile(r'(<!-- hat:dau[^>]*-->\n).*?(  <!-- hat:cuoi -->)', re.S)


def chup(html, name, scale=1):
    """Shoot html (text) with Chrome and return the gray image. Fonts resolve against HERE."""
    page = OUT / (name + '.html')
    png = OUT / (name + '.png')
    page.write_text(html.replace('<meta charset="utf-8">',
                                 '<meta charset="utf-8"><base href="%s/">' % HERE.as_uri(), 1))
    for _ in range(4):
        png.unlink(missing_ok=True)
        r = subprocess.run([CHROME, '--headless=new', '--disable-gpu', '--hide-scrollbars',
                            '--no-first-run', '--no-default-browser-check',
                            f'--user-data-dir={OUT / "chrome"}',
                            f'--force-device-scale-factor={scale}', f'--window-size={W},{H}',
                            '--default-background-color=FFFFFFFF', f'--screenshot={png}',
                            page.as_uri()], capture_output=True, timeout=60)
        if r.returncode == 0 and png.exists():
            break
    else:
        raise SystemExit('Chrome failed on ' + name)
    img = Image.open(png)
    bg = Image.new('RGB', img.size, '#FFFFFF')
    bg.paste(img, mask=img.split()[3] if img.mode == 'RGBA' else None)
    return bg.convert('L')


def doc_vung_cam(html):
    m = re.search(r'<!-- hat:dau vung-cam=(\[.*?\]) -->', html, re.S)
    return json.loads(m.group(1)) if m else []


def trong_vung(x, y, vung):
    return any(a <= x <= c and b <= y <= d for a, b, c, d in vung)


def rai_hat(nen, vung_cam):
    """SVG block of plus and x grains, tone measured from the background shot."""
    px = nen.load()
    r = random.Random(20260915)
    net = []
    for oy in range(0, H, O):
        for ox in range(0, W, O):
            cx = ox + r.uniform(5, O - 5)
            cy = oy + r.uniform(5, O - 5)
            f = 0.42 * (cx / W) + 0.58 * (cy / H)
            f = max(0.0, min(1.0, f + r.uniform(-0.12, 0.12)))
            nhuong = 0.22 if trong_vung(cx, cy, [O_MARK]) else 1.0
            if r.random() > TI_LE_CO_HAT * (0.40 + 0.85 * f) * nhuong:
                continue
            if trong_vung(cx, cy, vung_cam):
                continue
            nua = (HAT_NHO + (HAT_LON - HAT_NHO) * f) * r.uniform(0.75, 1.25) / 2
            if cx - nua < 6 or cx + nua > W - 6 or cy - nua < 6 or cy + nua > H - 6:
                continue
            tong = int(px[min(W - 1, int(cx)), min(H - 1, int(cy))])
            buoc = BUOC_NHAT * (0.30 + 0.48 * f) * (0.55 if nhuong < 1 else 1.0)
            lech = tong - buoc if tong >= 128 else tong + buoc
            lech = int(0 if lech < 0 else 255 if lech > 255 else lech)
            mau = f"#{lech:02X}{lech:02X}{lech:02X}"
            day = round(1.3 + 1.3 * f, 1)
            if r.random() < 0.5:
                d = f"M{cx-nua:.1f} {cy:.1f}H{cx+nua:.1f}M{cx:.1f} {cy-nua:.1f}V{cy+nua:.1f}"
            else:
                d = (f"M{cx-nua:.1f} {cy-nua:.1f}L{cx+nua:.1f} {cy+nua:.1f}"
                     f"M{cx-nua:.1f} {cy+nua:.1f}L{cx+nua:.1f} {cy-nua:.1f}")
            xoay = -15 + r.uniform(-7, 7)
            net.append(f'<path d="{d}" stroke="{mau}" stroke-width="{day}" stroke-linecap="square"'
                       f' transform="rotate({xoay:.1f} {cx:.1f} {cy:.1f})"/>')
    net.append(hat_bu(px))
    return ('  <svg class="hat-nen" width="528" height="792" viewBox="0 0 528 792" '
            'xmlns="http://www.w3.org/2000/svg" aria-hidden="true" '
            'style="position:absolute;inset:0;">\n    '
            + "\n    ".join(net) + "\n  </svg>\n")


def hat_bu(px):
    """The large plus: its tone is measured along its own body, half a level off the background."""
    cx, cy, nua, day = 104.0, 476.0, 196.0, 84.0
    tren, duoi = cy - nua - day / 2, cy + nua + day / 2
    xs = max(0, min(W - 1, int(cx + nua * 0.5)))
    moc = []
    for k in range(9):
        t = k / 8
        y = int(max(0, min(H - 1, tren + (duoi - tren) * t)))
        tong = int(px[xs, y])
        lech = tong - BUOC_BU if tong >= 128 else tong + BUOC_BU
        lech = 0 if lech < 0 else 255 if lech > 255 else lech
        moc.append(f'<stop offset="{t*100:.0f}%" stop-color="#{lech:02X}{lech:02X}{lech:02X}"/>')
    d = f"M{cx-nua:.1f} {cy:.1f}H{cx+nua:.1f}M{cx:.1f} {cy-nua:.1f}V{cy+nua:.1f}"
    return ('<defs><linearGradient id="bu" gradientUnits="userSpaceOnUse" '
            f'x1="0" y1="{tren:.1f}" x2="0" y2="{duoi:.1f}">' + "".join(moc) +
            '</linearGradient></defs>'
            f'<path d="{d}" stroke="url(#bu)" stroke-width="{day:.0f}" stroke-linecap="square"'
            f' transform="rotate(-15 {cx:.1f} {cy:.1f})"/>')


def rac_hat_panel(xam):
    """Firmware applyBayerDither4Level: Bayer 4x4 offset +-40, cuts at 64/128/192."""
    px = xam.load()
    ra = Image.new("L", xam.size)
    out = ra.load()
    for y in range(xam.size[1]):
        hang = BAYER[y & 3]
        for x in range(xam.size[0]):
            adj = px[x, y] + (hang[x & 3] - 8) * 5
            adj = 0 if adj < 0 else 255 if adj > 255 else adj
            out[x, y] = MUC[0 if adj < 64 else 1 if adj < 128 else 2 if adj < 192 else 3]
    return ra


def boot():
    html = (HERE / 'khoi-dong.html').read_text()
    return rac_hat_panel(chup(html, 'khoi-dong'))


def sleep():
    html = (HERE / 'muc-dang.html').read_text()
    vung = doc_vung_cam(html)
    sach = HAT_DAU.sub(r'\1\2', html)
    nen = chup(sach, 'muc-dang-nen')
    full = sach.replace('  <!-- hat:cuoi -->', rai_hat(nen, vung) + '  <!-- hat:cuoi -->')
    (OUT / 'muc-dang-full.html').write_text(full)
    bay = rac_hat_panel(chup(full, 'muc-dang-x1'))
    g4 = chup(full, 'muc-dang-x4', 4).resize((W, H), Image.Resampling.BOX)
    ink = tenor_ink4(np.array(g4))
    chu = HAT_DAU.sub(r'\1\2', full).replace('</style>', '.muc{display:none!important}</style>', 1)
    mask = binary_dilation(np.array(chup(chu, 'muc-dang-chu')) < 250, iterations=1)
    return Image.fromarray(np.where(mask, ink, np.array(bay)).astype(np.uint8)), full


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


if __name__ == '__main__':
    if not Path(CHROME).exists():
        sys.exit('Google Chrome is required to render.')
    OUT.mkdir(exist_ok=True)
    BMP.mkdir(exist_ok=True)
    b = boot()
    s, full = sleep()
    for im, name in ((b, BOOT_BMP), (s, SLEEP_BMP)):
        assert im.size == (W, H) and set(np.unique(np.array(im))) <= set(MUC)
        im.save(BMP / name, 'BMP')
        print(name, sha(BMP / name))
