"""X4 Pro sleep art in X4ProBrandAssets.h must equal scripts/brand-art/x4pro-sleep/x4pro-sleep.png (made by
that folder's render.py from the X3 sleep page), and packing it must leave the approved X4 Pro boot planes as they were."""
import re
import zlib
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
HEADER = (ROOT / 'src/components/X4ProBrandAssets.h').read_text()
W, H = 480, 800


def plane(name):
    data = re.search(r'\b' + name + r'\[\] = \{(.*?)\};', HEADER, re.S).group(1)
    raw = zlib.decompress(bytes(int(v, 16) for v in re.findall(r'0x([0-9a-f]{2})', data)))
    assert len(raw) == W * H // 8, name
    assert zlib.crc32(raw) == int(re.search(name + r'_CRC = 0x([0-9a-f]{8})', HEADER).group(1), 16), name + ' CRC'
    return np.unpackbits(np.frombuffer(raw, np.uint8))


def main():
    # The approved boot planes (c57d24ed).
    assert re.search(r'BOOT_LSB_CRC = 0xe7d26799', HEADER) and re.search(r'BOOT_MSB_CRC = 0xfb5e5a0f', HEADER)
    art = np.array(Image.open(ROOT / 'scripts/brand-art/x4pro-sleep/x4pro-sleep.png').convert('L'))
    assert art.shape == (H, W) and set(np.unique(art)) == {0, 85, 170, 255}
    # Native pixel (row r, column c) holds portrait pixel (x = W-1-r, y = c) (scripts/pack_x3_brand.py).
    levels = (plane('SLEEP_LSB') | plane('SLEEP_MSB') << 1).reshape(W, H)
    drawn = levels[::-1].T.astype(np.uint8) * 85
    differ = int((drawn != art).sum())
    assert differ == 0, f'{differ} pixels of the X4 Pro sleep planes differ from x4pro-sleep.png'
    page = (ROOT / 'scripts/brand-art/x4pro-sleep/render.py').read_text()
    assert "X3 / 'muc-dang.html'" in page, 'the X4 Pro sleep art is no longer drawn from the X3 sleep page'
    print('GREEN: X4 Pro sleep planes equal x4pro-sleep.png, boot planes unchanged')


if __name__ == '__main__':
    main()
