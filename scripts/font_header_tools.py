"""Read the uncompressed built-in font arrays for offline validation."""
import hashlib
import json
import re

def read_header(path):
    text = path.read_text(encoding='utf-8')
    def array(name):
        match = re.search(r'\w+' + name + r'\[.*?\]\s*=\s*\{(.*?)\n\};', text, re.S)
        return match.group(1) if match else ''
    bitmap = bytes(int(v, 16) for v in re.findall(r'0x([0-9a-fA-F]+)', array('Bitmaps')))
    glyphs = [tuple(map(int, re.findall(r'-?\d+', v))) for v in re.findall(r'\{([^{}]+)\}', array('Glyphs'))]
    intervals = [tuple(int(v.strip(), 0) for v in t.split(',')) for t in re.findall(r'\{([^{}]+)\}', array('Intervals'))]
    mapped = {}
    for lo, hi, offset in intervals:
        for cp in range(lo, hi + 1):
            g = glyphs[offset + cp - lo]
            mapped[cp] = (g[:5], bitmap[g[6]:g[6] + g[5]])
    tail = text[text.index('static const EpdFontData'):]
    fields = [x.strip() for x in re.sub(r'//[^\n]*', '', tail).split('{', 1)[1].split('}', 1)[0].split(',')]
    metrics = [int(x) for x in fields[4:7]]
    ancillary = re.findall(r'static const (?!EpdGlyph|EpdUnicodeInterval|EpdFontData)\w+ (\w+)\[.*?\]\s*=\s*\{(.*?)\n\};', text, re.S)
    hashes = {str(cp): hashlib.sha256(json.dumps(m).encode() + b).hexdigest() for cp, (m, b) in mapped.items()}
    kern = {n: hashlib.sha256(re.sub(r'\s', '', a).encode()).hexdigest() for n, a in ancillary if not n.endswith('Bitmaps')}
    return {'glyphs': mapped, 'hashes': hashes, 'metrics': metrics, 'kern': kern, 'bitmap_bytes': len(bitmap)}
