"""Do chong lan tren anh 1 bit cua gia lap: tach khoi muc that (QR, khung/vien, chu theo dong), roi do tung cap.
Khong dung rect bo tri trong ma. Vung thanh duoi (bar) cua X4 Pro lay theo he thiet ke (y 716..784)."""
import re
from pathlib import Path

import numpy as np
from scipy import ndimage as ndi

EDGE = {'x3': 4, 'x4pro': 8}
GAP = 4
S8 = np.ones((3, 3), bool)


class Blk:
    def __init__(self, kind, x0, y0, x1, y1, area=0, mask=None):
        self.kind, self.x0, self.y0, self.x1, self.y1, self.area = kind, x0, y0, x1, y1, area  # x1,y1 exclusive
        self.mask = mask  # mat na muc that trong hop (None = ca hop)

    w = property(lambda s: s.x1 - s.x0)
    h = property(lambda s: s.y1 - s.y0)

    def box(self):
        return [self.x0, self.y0, self.x1, self.y1]

    def __repr__(self):
        return f'{self.kind}{self.box()}'


def inside(a, b, m=0):  # a nam trong b
    return a.x0 >= b.x0 + m and a.y0 >= b.y0 + m and a.x1 <= b.x1 - m and a.y1 <= b.y1 - m


def ink_near(a, b, reach):
    """Muc that cua a va b co cach nhau <= reach px (Chebyshev) khong. QR, vien tron: dung mat na that hoac hop."""
    x0, y0 = max(a.x0, b.x0) - reach, max(a.y0, b.y0) - reach
    x1, y1 = min(a.x1, b.x1) + reach, min(a.y1, b.y1) + reach
    if x1 <= x0 or y1 <= y0:
        return False

    def crop(blk):
        full = np.zeros((blk.h, blk.w), bool) if blk.mask is None else blk.mask
        if blk.mask is None:
            full[:] = True
        out = np.zeros((y1 - y0, x1 - x0), bool)
        sx0, sy0 = max(blk.x0, x0), max(blk.y0, y0)
        sx1, sy1 = min(blk.x1, x1), min(blk.y1, y1)
        if sx1 > sx0 and sy1 > sy0:
            out[sy0 - y0:sy1 - y0, sx0 - x0:sx1 - x0] = full[sy0 - blk.y0:sy1 - blk.y0, sx0 - blk.x0:sx1 - blk.x0]
        return out
    ca, cb = crop(a), crop(b)
    return bool((ndi.binary_dilation(ca, structure=np.ones((3, 3), bool), iterations=reach) & cb).any())


def gaps(a, b):
    dx = max(0, max(a.x0, b.x0) - min(a.x1, b.x1))
    dy = max(0, max(a.y0, b.y0) - min(a.y1, b.y1))
    inter = (min(a.x1, b.x1) - max(a.x0, b.x0), min(a.y1, b.y1) - max(a.y0, b.y0))
    return dx, dy, inter



MOD, QR_PX = 6, 198
_FIND = None


def find_qr(M):
    """Tra ve list (x0, y0) goc tren trai cua moi QR version 4 (module 6 px) tim thay, can du ca 3 o dinh vi."""
    global _FIND
    from scipy.signal import fftconvolve
    if _FIND is None:
        t = np.zeros((7, 7), int)
        t[0, :] = t[6, :] = t[:, 0] = t[:, 6] = 1
        t[2:5, 2:5] = 1
        _FIND = np.kron(t, np.ones((MOD, MOD), int)) * 2 - 1
    k = _FIND
    score = fftconvolve((M.astype(np.float32) * 2 - 1), k[::-1, ::-1].astype(np.float32), mode='valid')
    ys, xs = np.where(score >= 0.93 * k.size)
    pts = sorted(set(zip(xs.tolist(), ys.tolist())))
    # gom diem sat nhau
    cl = []
    for x, y in pts:
        for c in cl:
            if abs(c[0] - x) <= 3 and abs(c[1] - y) <= 3:
                break
        else:
            cl.append((x, y))
    out = []
    off = QR_PX - 7 * MOD
    for x, y in cl:
        has_r = any(abs(x2 - (x + off)) <= 3 and abs(y2 - y) <= 3 for x2, y2 in cl)
        has_b = any(abs(x2 - x) <= 3 and abs(y2 - (y + off)) <= 3 for x2, y2 in cl)
        if has_r and has_b:
            out.append((x, y))
    return out


def segment(img, device):
    """img: PIL 'L'. Tra ve (blocks, M). Khoi: qr, ring (khung/vien/vien tron), rule, arrow, text (dong chu)."""
    M = np.array(img.convert('L')) < 128
    H, W = M.shape
    blocks = []
    # QR: tim 3 o dinh vi (finder 7x7 module, module 6 px, QR 33 module = 198 px) bang tuong quan, khong dung rect trong ma
    qrmask = np.zeros_like(M)
    for x0, y0 in find_qr(M):
        blocks.append(Blk('qr', x0, y0, x0 + QR_PX, y0 + QR_PX, QR_PX * QR_PX // 2, None))
        qrmask[y0:y0 + QR_PX, x0:x0 + QR_PX] = True
    rest = M & ~qrmask
    if device == 'x3':  # gach duoi nhan nut o 2 hang cuoi cua man: khoi rieng, khong dinh vao chu
        foot = np.zeros_like(M)
        foot[H - 3:, :] = True
        under = rest & foot
        if under.any():
            lu, _ = ndi.label(ndi.binary_dilation(under, structure=np.ones((1, 9), bool)))
            for sl in ndi.find_objects(lu):
                blocks.append(Blk('underline', sl[1].start, sl[0].start, sl[1].stop, sl[0].stop))
        rest = rest & ~foot
    lab, n = ndi.label(rest, structure=S8)
    drop = []
    for i, sl in enumerate(ndi.find_objects(lab), 1):
        y0, y1, x0, x1 = sl[0].start, sl[0].stop, sl[1].start, sl[1].stop
        w, h = x1 - x0, y1 - y0
        comp = lab[sl] == i
        area = int(comp.sum())
        kind = None
        if h <= 3 and w >= 0.4 * W:
            kind = 'rule'
        elif min(w, h) >= 36 and int(ndi.binary_fill_holes(ndi.binary_dilation(np.pad(comp, 3), structure=S8, iterations=2)).sum()) > 3 * area:
            kind = 'ring'
        elif h <= 2 and w >= 16:
            kind = 'underline'
        elif device == 'x3' and w <= 10 and h <= 22 and (x1 <= 14 or x0 >= W - 14):
            kind = 'arrow'
        if kind:
            blocks.append(Blk(kind, x0, y0, x1, y1, area, comp.copy()))
            drop.append(i)
    G = rest & ~np.isin(lab, drop)
    if G.any():
        l0, _ = ndi.label(G, structure=S8)
        hs = sorted(sl[0].stop - sl[0].start for sl in ndi.find_objects(l0))
        gh = max(8, hs[len(hs) // 2])
        hd = max(7, int(gh * 0.6))
        merged = ndi.binary_dilation(G, structure=np.ones((7, 2 * hd + 1), bool))  # 3 px doc: dau thanh dinh vao chu
        l2, n2 = ndi.label(merged, structure=S8)
        for i, sl in enumerate(ndi.find_objects(l2), 1):
            ink = G[sl] & (l2[sl] == i)
            ys, xs = np.where(ink)
            if len(ys) == 0:
                continue
            blocks.append(Blk('text', int(sl[1].start + xs.min()), int(sl[0].start + ys.min()),
                              int(sl[1].start + xs.max() + 1), int(sl[0].start + ys.max() + 1), int(ink.sum()),
                              ink[ys.min():ys.max() + 1, xs.min():xs.max() + 1].copy()))
    return blocks, M


def bar_zone(device, trace=None):
    """Vung thanh duoi: (y0, y1, [pill boxes]). X4 Pro theo he thiet ke; X3 theo vet [X3PATH]/[X3PILL] (chi dinh vi)."""
    if device == 'x4pro':
        return 716, 784, []
    if trace:
        bx, by, bw, bh, pills = trace
        return by, by + bh, pills
    return None


def measure(img, device, trace=None, tier=0):
    """Tra ve list loi: dict(kind, detail, boxes)."""
    blocks, M = segment(img, device)
    H, W = M.shape
    issues = []
    edge = EDGE[device]

    def add(kind, detail, *bs):
        issues.append({'kind': kind, 'detail': detail, 'boxes': [b.box() for b in bs]})

    # 1. cham mep
    for b in blocks:
        if b.kind in ('rule', 'arrow', 'underline'):
            continue
        if device == 'x3' and b.y0 >= H - 24:
            continue  # dai nut (ky hieu nut, pin, gio): hinh ky hieu thuoc viec khac, xem GHI NHAN
        d = min(b.x0, b.y0, W - b.x1, H - b.y1)
        if d < edge:
            add('mep', f'{b.kind} cach mep {d}px (can >= {edge})', b)
    # 2. cap khoi
    for i, a in enumerate(blocks):
        for b in blocks[i + 1:]:
            ka, kb = a.kind, b.kind
            if ('rule' in (ka, kb) or 'underline' in (ka, kb)) and not ('qr' in (ka, kb)):
                continue
            if 'arrow' in (ka, kb):
                continue
            for u, v in ((a, b), (b, a)):
                if u.kind == 'ring' and inside(v, u):
                    m = min(v.x0 - u.x0, v.y0 - u.y0, u.x1 - v.x1, u.y1 - v.y1)
                    if m < GAP and v.kind != 'ring':
                        add('sat-vien', f'{v.kind} cach vien trong {m}px', u, v)
                    break
            else:
                dx, dy, (ix, iy) = gaps(a, b)
                if max(dx, dy) >= GAP:
                    continue
                if ka == 'text' and kb == 'text':
                    # hai dong chong len nhau (giao) hoac hai cot sat nhau, do tren muc that
                    if ink_near(a, b, GAP - 1):
                        add('ha' if ix <= 0 or iy <= 0 else 'giao', 'hai khoi chu sat/giao nhau (muc cach < 4 px)', a, b)
                    continue
                if ink_near(a, b, GAP - 1):
                    add('giao' if (ix > 0 and iy > 0) else 'ha', f'{ka} sat/giao {kb} (muc cach < {GAP} px)', a, b)
    if device == 'x3':
        issues.extend(band_gaps(M, tier))
    else:
        issues.extend(header_bars(M))
    # 3. vung thanh duoi: moi thu khong thuoc thanh khong duoc bat qua dinh vung
    z = bar_zone(device, trace)
    if z:
        zy0, zy1, pills = z
        for b in blocks:
            if b.kind in ('rule', 'underline'):
                continue
            in_zone = b.y0 >= zy0 + (8 if device == 'x4pro' else 0)
            if device == 'x4pro':
                if b.y0 < zy0 < b.y1 or (b.y1 > zy0 and b.y0 < zy0 + 8 and not in_zone):
                    add('thanh', f'{b.kind} chui vao vung thanh y {b.y0}..{b.y1} (thanh tu {zy0 + 8})', b)
            else:
                if b.y0 < zy1 < b.y1 and b.kind != 'ring':
                    add('thanh', f'{b.kind} vuot duong day thanh y {zy1}', b)
                elif zy0 <= b.y0 and b.y1 <= zy1 and b.kind == 'text':
                    if not any(inside(b, Blk('p', *p)) for p in pills):
                        add('thanh', f'chu nam trong thanh ngoai vien path', b)
    return issues, blocks


# Do tren anh man khong co nhan nut: x cuoi cua pin '100' va be rong lon nhat cua dong ho theo co chu.
BATTERY_END = {0: 65, 1: 73, 2: 79}
CLOCK_MAX_W = {0: 48, 1: 58, 2: 69}


def band_gaps(M, tier=0):
    """X3, dai nut (y H-32..H-3): pin '100' o trai va dong ho 'HH:MM' o phai la cum cot muc gom cac chu cach <= 3 px.
    Nhan nut cach pin hoac dong ho < 4 px thi dinh vao cum do, lam cum vuot be rong cua pin hoac dong ho."""
    H, W = M.shape
    cols = M[H - 32:H - 3].any(0)
    cl, st = [], None
    for x, v in enumerate(cols):
        if v and st is None:
            st = x
        if not v and st is not None:
            cl.append((st, x))
            st = None
    if st is not None:
        cl.append((st, W))
    out = []
    if not cl:
        return out
    end = cl[0][1]
    for a, b in cl[1:]:
        if a - end > 3:
            break
        end = b
    if end > BATTERY_END[tier] + 1 and cl[0][0] <= 16:
        out.append({'kind': 'ha', 'detail': f'nhan nut cach so pin < 4px (cum pin keo toi x {end}, pin het o {BATTERY_END[tier]})',
                    'boxes': [[cl[0][0], H - 32, end, H - 3]]})
    start = cl[-1][0]
    for a, b in reversed(cl[:-1]):
        if start - b > 3:
            break
        start = a
    if cl[-1][1] - start > CLOCK_MAX_W[tier] + 2 and cl[-1][1] >= W - 16:
        out.append({'kind': 'ha', 'detail': f'nhan nut cach dong ho < 4px (cum gio keo toi x {start})',
                    'boxes': [[start, H - 32, cl[-1][1], H - 3]]})
    return out


def header_bars(M):
    """X4 Pro: o ten mang (hang phu duoi dong ho) co bieu tuong song Wi-Fi o cuoi phai: 4 vach rong 4 px cach nhau 2 px, cao
    3, 7, 10, 14 px, cung day. Ky hieu cach chu ben canh >= 6 px (TC-KEY-02). Nhan ra bang 3 vach cuoi (vach thap nhat co
    the dinh vao dau '...' neu chu sat), roi do tu mep trai cua vach thap nhat."""
    H, W = M.shape
    y0, y1 = 36, 70
    band = M[y0:y1]
    cols = band.any(0)
    cl, st = [], None
    for x, v in enumerate(cols):
        if v and st is None:
            st = x
        if not v and st is not None:
            cl.append((st, x))
            st = None
    if st is not None:
        cl.append((st, W))
    if len(cl) < 3:
        return []
    last = cl[-3:]
    if any(b - a != 4 for a, b in last) or last[1][0] - last[0][1] != 2 or last[2][0] - last[1][1] != 2:
        return []
    ext = []
    for a, b in last:
        ys = np.where(band[:, a:b].any(1))[0]
        ext.append((int(ys.min()), int(ys.max())))
    if len({e[1] for e in ext}) != 1 or [e[1] - e[0] + 1 for e in ext] != [7, 10, 14]:
        return []
    left = last[0][0] - 6  # vach thap nhat
    other = np.where(band[:, :left].any(0))[0]
    if len(other) == 0:
        return []
    gap = left - int(other.max()) - 1
    if gap < 6:
        return [{'kind': 'ha', 'detail': f'ten mang cach bieu tuong song {gap}px (can >= 6)',
                 'boxes': [[int(other.max()) - 10, y0, last[-1][1], y1]]}]
    return []


def parse_trace(log, shot_name):
    """Lay [X3PATH] + [X3PILL] cuoi cung truoc dong luu anh co ten tep shot_name (vd '9500.bmp'). Chi dung de dinh vi vung thanh."""
    hit = re.search(r'Saved screenshot: \S*/' + re.escape(Path(str(shot_name)).name), log)
    head = log[:hit.start()] if hit else log
    i = head.rfind('[X3PATH]')
    if i < 0:
        return None
    seg = head[i:]
    m = re.search(r'bar=(\d+),(\d+),(\d+),(\d+)', seg)
    pills = [(int(a), int(b), int(a) + int(c), int(b) + int(d)) for a, b, c, d in
             re.findall(r'\[X3PILL\] x=(\d+) y=(\d+) w=(\d+) h=(\d+)', seg)]
    return (*map(int, m.groups()), pills)
