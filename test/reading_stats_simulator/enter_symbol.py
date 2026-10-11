"""Find the tenor/cross Enter symbols (kSelectNet14 and kSelectNet18 of lib/GfxRenderer/InlineSymbolBitmaps.h) in a
screenshot: every ink pixel of the bitmap black. find(im) -> [(name, x, y)]."""
import re, sys
from pathlib import Path
from PIL import Image
HEADER = Path(__file__).resolve().parents[2] / 'lib/GfxRenderer/InlineSymbolBitmaps.h'

def glyphs():
    text = HEADER.read_text()
    out = []
    for name in ('kSelectNet14', 'kSelectNet18'):
        rows = [int(v, 16) for v in re.search(name + r'Rows\[\] = \{(.*?)\};', text, re.S).group(1).replace('\n', '').split(',') if v.strip()]
        w, h = re.search(r'Glyph ' + name + r'\{' + name + r'Rows, (\d+), (\d+)', text).groups()
        w, h = int(w), int(h)
        out.append((name, [(c, r) for r, bits in enumerate(rows) for c in range(w) if bits & (1 << (w - 1 - c))], w, h))
    return out

def find(im):
    im = im.convert('L'); px = im.load(); W, H = im.size
    hits = []
    for name, pts, w, h in glyphs():
        for y in range(H - h):
            for x in range(W - w):
                if all(px[x + c, y + r] < 128 for c, r in pts):
                    hits.append((name, x, y))
    return hits

if __name__ == '__main__':
    for p in sys.argv[1:]:
        print(p, find(Image.open(p)) or 'none')
