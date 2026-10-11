#!/usr/bin/env python3
"""Render the gray master of the cover placeholder: python3 render.py <sleep-art.html> <out.png>

The sleep art of the X3 (collateral 260915, "muc-dang") with its lockup hidden, drawn by Chrome at
device scale factor 4 and reduced in one step to 298x450 (the largest cover frame): the Y channel
(BT.601, no gamma change) BOX-averaged with the centred crop that fills the frame. The result is
8-bit gray, not dithered; the firmware reduces and dithers it like any cover (tenor/ink).
"""
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image

CHROME = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'
W, H = 298, 450


def main(html: Path, out: Path) -> None:
    with tempfile.TemporaryDirectory() as tmp:
        page = Path(tmp) / 'page.html'
        page.write_text(html.read_text().replace('</style>', '.lockup{display:none}</style>', 1))
        shot = Path(tmp) / 'shot.png'
        # Chrome may crash while tearing down after the screenshot is written, so the exit code is not checked.
        subprocess.run([CHROME, '--headless=new', '--disable-gpu', '--hide-scrollbars', '--no-first-run',
                        f'--user-data-dir={tmp}/profile', '--force-device-scale-factor=4', '--window-size=528,792',
                        '--default-background-color=FFFFFFFF', f'--screenshot={shot}', page.as_uri()],
                       capture_output=True, timeout=60)
        rgb = np.asarray(Image.open(shot).convert('RGB'), dtype=np.uint32)
    y = ((19595 * rgb[..., 0] + 38470 * rgb[..., 1] + 7471 * rgb[..., 2]) >> 16).astype(np.uint8)
    sh, sw = y.shape
    s = max(W / sw, H / sh)
    cw, ch = W / s, H / s
    box = (max(0.0, (sw - cw) / 2), max(0.0, (sh - ch) / 2), min(sw, (sw + cw) / 2), min(sh, (sh + ch) / 2))
    Image.fromarray(y).resize((W, H), Image.Resampling.BOX, box=box).save(out)


if __name__ == '__main__':
    main(Path(sys.argv[1]), Path(sys.argv[2]))
