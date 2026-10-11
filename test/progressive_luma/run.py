"""Progressive luma decoder (X3 covers): generated files, never book covers.

Right: 4:2:0, 4:4:4, 4:2:2, gray, DC split by component, restart markers, successive approximation,
a SOF behind 70 KB of APP segments; every one within 40 dB of libjpeg (Pillow) luma.
Refused for good: CMYK, too wide, more luma scans than readers, more than 64 scans, chroma wider
than luma, zero height, baseline. Damaged: cut, bad lengths, bad table ids, overfull and
unmatched Huffman codes, bad spectral bounds, no SOF, restart out of order, then random damage
under the address and behaviour sanitizers. Stops within 16 blocks; a refused block holds nothing.
"""
import io, math, os, random, shutil, subprocess, sys, tempfile
from pathlib import Path
from PIL import Image, ImageChops, ImageDraw, ImageStat

HOST = sys.argv[1]
JPEGTRAN = shutil.which('jpegtran') or '/opt/homebrew/bin/jpegtran'
assert Path(JPEGTRAN).exists(), 'jpegtran (libjpeg-turbo) is needed to write the scan scripts'
work = Path(tempfile.mkdtemp(prefix='pjx', dir=os.environ.get('TMPDIR')))
fails = []


def host(*args):
    r = subprocess.run([HOST, *map(str, args)], capture_output=True, text=True, timeout=60)
    if r.returncode != 0 or 'runtime error' in r.stderr or 'Sanitizer' in r.stderr:
        fails.append(f'{args}: rc={r.returncode} {r.stderr[-400:]}')
        return {}
    return dict(kv.split('=', 1) for kv in r.stdout.split())


def check(ok, what):
    print(('GREEN ' if ok else 'RED ') + what)
    if not ok:
        fails.append(what)


def picture(w, h, seed=7):
    rng = random.Random(seed)
    across = Image.linear_gradient('L').rotate(90).resize((w, h))
    down = Image.linear_gradient('L').resize((w, h))
    img = Image.merge('RGB', [across, down, Image.effect_noise((w, h), 60)])
    d = ImageDraw.Draw(img)
    for _ in range(25):  # hard edges, like letters on a cover
        x0, y0 = rng.randrange(0, w - 12), rng.randrange(0, h - 6)
        d.rectangle([x0, y0, x0 + rng.randrange(4, 12), y0 + rng.randrange(2, 6)], fill=tuple(rng.randrange(256) for _ in range(3)))
    return img


def save(name, data):
    p = work / f'{name}.jpg'
    p.write_bytes(data)
    return p


def pillow(img, **kw):
    b = io.BytesIO()
    img.save(b, 'JPEG', progressive=True, quality=88, **kw)
    return b.getvalue()


def jpegtran(data, *args, script=None):
    src = save('_src', data)
    cmd = [JPEGTRAN, *args]
    if script:
        (work / 'scans.txt').write_text(script)
        cmd += ['-scans', str(work / 'scans.txt')]
    return subprocess.run(cmd + [str(src)], capture_output=True, check=True).stdout


def segments(data):
    """[(marker, payload)] up to the first SOS, the SOS payload, then the rest from its entropy data."""
    out, p = [], 2
    while True:
        assert data[p] == 0xFF
        m, n = data[p + 1], data[p + 2] << 8 | data[p + 3]
        out.append((m, data[p + 4:p + 2 + n]))
        p += 2 + n
        if m == 0xDA:
            return out, data[p:]


def join(segs, rest):
    return b'\xff\xd8' + b''.join(bytes([0xFF, m]) + (len(pl) + 2).to_bytes(2, 'big') + pl for m, pl in segs) + rest


def edit(data, marker, fn, nth=0):
    segs, rest = segments(data)
    i = [k for k, (m, _) in enumerate(segs) if m == marker][nth]
    segs[i] = (marker, fn(bytearray(segs[i][1])))
    return join(segs, rest)


def psnr(path):
    a = Image.open(work / 'out.pgm')
    ref = Image.open(path)
    ref.draft('YCbCr', ref.size)
    y = ref.convert('YCbCr').getchannel(0) if ref.mode != 'L' else ref
    if a.size != y.size:
        return 0.0
    mse = sum(ImageStat.Stat(ImageChops.difference(a, y)).sum2) / (a.width * a.height)
    return 99.0 if mse == 0 else 10 * math.log10(255 ** 2 / mse)


rgb = picture(203, 307)
p420 = pillow(rgb, subsampling=2)
right = {
    'p420': p420,
    'p444': pillow(rgb, subsampling=0),
    'p422': pillow(rgb, subsampling=1),
    'gray': pillow(rgb.convert('L')),
    # DC of each component in a scan of its own, then AC; one refinement pass.
    'dc_split': jpegtran(p420, '-copy', 'none', script='0: 0 0 0 1;\n1: 0 0 0 0;\n2: 0 0 0 0;\n0: 1 9 0 0;\n0: 10 63 0 0;\n'
                         '1: 1 63 0 0;\n2: 1 63 0 0;\n0: 0 0 1 0;\n'),
    'dri_rows': jpegtran(p420, '-progressive', '-restart', '1'),
    'dri_blocks': jpegtran(pillow(rgb, subsampling=0), '-progressive', '-restart', '7B'),
    # Every AC band in successive approximation, refinement scans of luma in between.
    'sa': jpegtran(p420, '-copy', 'none', script='0 1 2: 0 0 0 2;\n0: 1 5 0 2;\n0: 6 63 0 2;\n1: 1 63 0 1;\n2: 1 63 0 1;\n'
                   '0: 1 63 2 1;\n0 1 2: 0 0 2 1;\n0 1 2: 0 0 1 0;\n0: 1 63 1 0;\n1: 1 63 1 0;\n2: 1 63 1 0;\n'),
    'wide_odd': pillow(picture(1701, 21, 3), subsampling=2),
}
segs, rest = segments(p420)
right['far_sof'] = join([segs[0], (0xE1, b'Exif\0\0' + os.urandom(30000)), (0xE2, b'ICC_PROFILE\0' + bytes(40000))] + segs[1:], rest)
for name, data in right.items():
    p = save(name, data)
    r = host('decode', p, work / 'out.pgm')
    db = psnr(p) if r.get('run') == 'Done' else 0
    check(r.get('run') == 'Done' and db >= 40, f'{name}: {r.get("open")}/{r.get("run")} {db:.1f} dB against libjpeg')
s = host('sniff', save('far', right['far_sof']))
check(s.get('kind') == 'Progressive' and s.get('w') == '203' and s.get('h') == '307', f'far SOF sniffed: {s}')
check(host('sniff', save('pgray', right['gray'])).get('kind') == 'Progressive', 'gray sniffed progressive')

base = io.BytesIO()
rgb.save(base, 'JPEG', quality=88)
cmyk = io.BytesIO()
rgb.convert('CMYK').save(cmyk, 'JPEG', progressive=True)
many_luma = ''.join(f'0: {k} {k} 0 0;\n' for k in range(1, 10)) + '0: 10 63 0 0;\n'
many = ''.join(f'1: {k} {k} 0 0;\n' for k in range(1, 64)) + '2: 1 63 0 0;\n0: 1 63 0 0;\n'  # 66 scans, 2 of luma
unsupported = {
    'cmyk': cmyk.getvalue(),
    'too_wide': pillow(picture(2100, 16, 5)),
    'nine_luma_scans': jpegtran(p420, '-copy', 'none', script='0 1 2: 0 0 0 0;\n' + many_luma + '1: 1 63 0 0;\n2: 1 63 0 0;\n'),
    'scans_over_64': jpegtran(p420, '-copy', 'none', script='0 1 2: 0 0 0 0;\n' + many),
    'chroma_wider': edit(p420, 0xC2, lambda b: b[:7] + bytes([0x11]) + b[8:10] + bytes([0x22]) + b[11:]),
    'zero_height': edit(p420, 0xC2, lambda b: b[:1] + bytes([0, 0]) + b[3:]),
    'baseline': base.getvalue(),
}
for name, data in unsupported.items():
    r = host('decode', save(name, data), work / 'out.pgm')
    check(r.get('open') == 'Unsupported' and r.get('live') == '0', f'{name}: refused for good, nothing held ({r})')
check(host('sniff', save('b', base.getvalue())).get('kind') == 'Baseline', 'baseline sniffed baseline')
check(host('sniff', save('c', cmyk.getvalue())).get('kind') == 'Unsupported', 'CMYK sniffed unsupported')


def table(cls, tid, counts, values):
    return bytes([cls << 4 | tid]) + bytes(counts) + bytes(values)


rst = right['dri_rows']
r_at = rst.index(b'\xff\xd1', rst.index(b'\xff\xda'))
dht = [k for k, (m, _) in enumerate(segments(p420)[0]) if m == 0xC4][0]
corrupt = {
    'cut_in_entropy': p420[:len(p420) * 6 // 10],
    'length_0': p420[:4] + b'\x00\x00' + p420[6:],
    'length_1': p420[:4] + b'\x00\x01' + p420[6:],
    'length_past_end': p420[:2] + b'\xff\xe0\xff\xff' + p420[2:200],
    'huffman_id_4': edit(p420, 0xC4, lambda b: bytes([b[0] & 0xF0 | 4]) + b[1:]),
    'quant_id_4': edit(p420, 0xDB, lambda b: bytes([4]) + b[1:]),
    'huffman_overfull': edit(p420, 0xC4, lambda b: b[:1] + bytes([3]) + b[2:]),
    # Luma DC (its first table) with one code of 16 zeros: the data's first code matches nothing.
    'huffman_no_match': edit(p420, 0xC4, lambda b: table(0, b[0] & 15, [0] * 15 + [1], [0]) + b[17 + sum(b[1:17]):]),
    'no_sof': join([s for s in segments(p420)[0] if s[0] != 0xC2], segments(p420)[1]),
    'restart_order': rst[:r_at + 1] + b'\xd3' + rst[r_at + 2:],
}


def scan_header(data, nth, fn):
    """Edit the nth SOS header in place (any scan, not only the first)."""
    p = -1
    for _ in range(nth + 1):
        p = data.index(b'\xff\xda', p + 1)
    n = data[p + 2] << 8 | data[p + 3]
    head = bytearray(data[p + 4:p + 2 + n])
    return data[:p + 4] + bytes(fn(head)) + data[p + 2 + n:]


corrupt['ss_over_se'] = scan_header(p420, 1, lambda h: h[:-3] + bytes([5, 1, h[-1]]))
corrupt['se_over_63'] = scan_header(p420, 1, lambda h: h[:-3] + bytes([1, 64, h[-1]]))
# The band of the first luma AC scan narrowed to 1 coefficient: its runs land past the band's end.
corrupt['ac_run_past_se'] = scan_header(p420, 1, lambda h: h[:-3] + bytes([1, 1, h[-1]]))
# The luma band 6..63 read as 60..63: its runs reach past coefficient 63.
corrupt['ac_run_past_63'] = scan_header(p420, 4, lambda h: h[:-3] + bytes([60, 63, h[-1]]))
# Luma DC with every code meaning a 15 bit difference: the coefficients run far past what a picture
# gives. The decode may end any way, but its arithmetic stays defined (the sanitizers watch).
dc15 = edit(p420, 0xC4, lambda b: table(0, b[0] & 15, [1] + [0] * 15, [15]) + b[17 + sum(b[1:17]):])
r = host('decode', save('dc15', dc15), work / 'out.pgm')
check(bool(r) and r.get('open') == 'Done', f'DC of category 15: decoded without undefined arithmetic ({r})')
# Luma quantized by 65.535 everywhere (a 16 bit table): every product leaves 32 bits in the IDCT.
q16 = edit(p420, 0xDB, lambda b: bytes([0x10 | (b[0] & 15)]) + b'\xff\xff' * 64 + b[65:])
r = host('decode', save('q16', q16), work / 'out.pgm')
check(r.get('run') == 'Done', f'16 bit quantizers of 65.535: decoded without undefined arithmetic ({r})')
# A smooth picture framed shorter than its scans: an end-of-band run reaches past the last block.
smooth = io.BytesIO()
Image.linear_gradient('L').resize((203, 307)).convert('RGB').save(smooth, 'JPEG', progressive=True, quality=88)
corrupt['eob_run_past_end'] = edit(smooth.getvalue(), 0xC2, lambda b: b[:1] + (64).to_bytes(2, 'big') + b[3:])
# Cut right before the last scan: the scans before it decode whole, only the missing end tells.
corrupt['cut_at_scan'] = p420[:p420.rindex(b'\xff\xda')]
# The DC refinement scan (1 bit a block, no codes) losing half its data, the file going on to its end:
# only the count of bits read past the data tells.
starts = [i for i in range(len(p420) - 1) if p420[i] == 0xFF and p420[i + 1] == 0xDA]
s6 = starts[6] + 2 + (p420[starts[6] + 2] << 8 | p420[starts[6] + 3])
corrupt['scan_short'] = p420[:s6] + p420[s6:(s6 + starts[7]) // 2] + p420[starts[7]:]
# A Huffman table counting one more code than its segment holds.
corrupt['huffman_count_past_segment'] = edit(p420, 0xC4, lambda b: b[:16] + bytes([b[16] + 1]) + b[17:])
corrupt['ah_over_13'] = scan_header(p420, 1, lambda h: h[:-3] + bytes([h[-3], h[-2], 0xE0 | 13]))
for name, data in corrupt.items():
    r = host('decode', save(name, data), work / 'out.pgm')
    bad = r.get('open') == 'Corrupt' or r.get('run') == 'Corrupt'
    check(bad, f'{name}: Corrupt ({r})')

# Random damage: any answer, but no read or write outside the file and the buffers.
rng = random.Random(58)
for k in range(240):
    src = bytearray(rng.choice([p420, rst, right['sa'], right['gray']]))
    for _ in range(rng.randint(1, 12)):
        at = rng.randrange(2, len(src))
        src[at] = rng.randrange(256) if rng.random() < 0.8 else 0xFF
    if rng.random() < 0.3:
        src = src[:rng.randrange(10, len(src))]
    before = len(fails)
    host('decode', save('fuzz', bytes(src)), work / 'out.pgm')
    host('sniff', work / 'fuzz.jpg')
    if len(fails) > before:
        save(f'fuzz_fail_{k}', bytes(src))
check(not any('fuzz' in f or 'rc=' in f for f in fails), '240 randomly damaged files: no sanitizer report, no crash')

# The marker pass asks before every sector: a stop there has read no further than the sectors before it.
for n in (1, 5):
    r = host('stop', save('p420', p420), n)
    check(r.get('open') == 'Stopped' and int(r.get('read', 1 << 30)) <= (n - 1) * 512 and r.get('live') == '0',
          f'stop at question {n}, in the marker pass: {r}')
n = 120
r = host('stop', save('p420', p420), n)
check(r.get('run') == 'Stopped' and int(r.get('max_gap', 99)) <= 16 and int(r.get('after', 99)) <= 1,
      f'stop at question {n}, in the decode: {r}')
for door in ('pjx-readers', 'pjx-tables', 'pjx-dc', 'pjx-band'):
    r = host('refuse', save('p420', p420), door)
    check(r.get('open') == 'NoHeap' and r.get('live') == '0', f'heap floor refuses {door}: nothing held ({r})')

shutil.rmtree(work, ignore_errors=True)
if fails:
    print('\n'.join(fails))
sys.exit(1 if fails else 0)
