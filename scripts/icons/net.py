"""May ve icon 1 bit theo khoang cach: moi diem anh hoi tam cua no cach duong tam net bao xa.

Hinh khai tren luoi 24 (nhu Lucide), nhan theo co, roi ghim toa do vao tam diem anh (net le) hoac mep
diem anh (net chan). Net day dung w diem anh o moi huong, cong hay cheo; dau net va goc gay tron.
"""
import math
from PIL import Image

EPS = 1e-6


def _seg(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    L = dx * dx + dy * dy
    t = 0 if L == 0 else max(0, min(1, ((px - ax) * dx + (py - ay) * dy) / L))
    return math.hypot(px - ax - t * dx, py - ay - t * dy)


CAP = 0.2   # dau net, goc gay dung ban kinh nho hon nua net 0,2 px: net 3 bo 2 diem goc, dau ra tron


def _net(px, py, a, b, h):
    """Trung than doan ab (khoang cach vuong goc < h) hoac trung dau tron (cach dau mut < h - CAP)."""
    ax, ay = a
    bx, by = b
    dx, dy = bx - ax, by - ay
    L = dx * dx + dy * dy
    if L:
        t = ((px - ax) * dx + (py - ay) * dy) / L
        if 0 <= t <= 1 and abs((px - ax) * dy - (py - ay) * dx) / math.sqrt(L) < h:
            return True
    hc = h - CAP
    return math.hypot(px - ax, py - ay) < hc or math.hypot(px - bx, py - by) < hc


def _inpoly(px, py, pts):
    c = False
    for i in range(len(pts)):
        (x0, y0), (x1, y1) = pts[i], pts[i - 1]
        if (y0 > py) != (y1 > py) and px < (x1 - x0) * (py - y0) / (y1 - y0) + x0:
            c = not c
    return c


class G:
    def __init__(s, n, w=3, snap=True, u=None):
        s.n, s.w, s.u = n, w, (u or n / 24.0)
        s.off = 0.5 if w % 2 else 0.0
        s.snap = snap
        s.ops = []                 # (1 = muc, 0 = xoa, ham (x, y) -> True neu trung), ve theo thu tu

    # ---- doi toa do luoi 24 sang diem anh, ghim ----
    def X(s, v):
        p = v * s.u
        return round(p - s.off) + s.off if s.snap else p

    def R(s, r):
        return round(r * s.u) if s.snap else r * s.u

    def P(s, x, y):
        return s.X(x), s.X(y)

    # ---- net ----
    def poly(s, *pts, closed=False, w=None, to=None):
        q = [s.P(*p) for p in pts]
        if closed:
            q.append(q[0])
        h = (w or s.w) / 2 - EPS
        segs = list(zip(q, q[1:]))
        (to if to is not None else s.ink).append(
            lambda x, y: any(_net(x, y, a, b, h) for a, b in segs))

    line = poly

    def rpoly(s, *pts, r=3, closed=False, w=None, fill=False, to=None):
        """Duong gap khuc co bo: moi goc gay thay bang cung tiep tuyen ban kinh r (don vi luoi, so hoac
        danh sach theo tung dinh). Ban kinh tu co khi canh ke qua ngan (toi da nua canh). fill=True thi to."""
        q = list(fillet([s.P(*p) for p in pts], [x * s.u for x in (r if isinstance(r, (list, tuple)) else [r] * len(pts))], closed))
        out = to if to is not None else s.ink
        if fill:
            out.append(lambda x, y: _inpoly(x, y, q))
            return
        if closed:
            q.append(q[0])
        h = (w or s.w) / 2 - EPS
        segs = list(zip(q, q[1:]))
        out.append(lambda x, y: any(_net(x, y, a, b, h) for a, b in segs))

    def circle(s, cx, cy, r, w=None, to=None):
        X, Y, R = s.X(cx), s.X(cy), s.R(r)
        h = (w or s.w) / 2 - EPS
        (to if to is not None else s.ink).append(lambda x, y: abs(math.hypot(x - X, y - Y) - R) < h)

    def arc(s, cx, cy, r, a0, a1, w=None):
        """Cung tu a0 toi a1 do (0 = phai, 90 = xuong), dau tron."""
        X, Y, R = s.X(cx), s.X(cy), s.R(r)
        h = (w or s.w) / 2 - EPS
        e = [(X + R * math.cos(math.radians(a)), Y + R * math.sin(math.radians(a))) for a in (a0, a1)]
        span = (a1 - a0) % 360

        def f(x, y):
            a = (math.degrees(math.atan2(y - Y, x - X)) - a0) % 360
            if a <= span:
                return abs(math.hypot(x - X, y - Y) - R) < h
            return any(math.hypot(x - ex, y - ey) < h - CAP for ex, ey in e)
        s.ink.append(f)

    def rrect(s, x0, y0, x1, y1, r=2, w=None, fill=False, to=None):
        a, b = s.P(x0, y0), s.P(x1, y1)
        cx, cy = (a[0] + b[0]) / 2, (a[1] + b[1]) / 2
        hx, hy = (b[0] - a[0]) / 2, (b[1] - a[1]) / 2
        R = s.R(r)
        h = (w or s.w) / 2 - EPS

        def sd(x, y):
            qx, qy = abs(x - cx) - (hx - R), abs(y - cy) - (hy - R)
            return math.hypot(max(qx, 0), max(qy, 0)) + min(max(qx, qy), 0) - R
        (to if to is not None else s.ink).append((lambda x, y: sd(x, y) < 0) if fill else (lambda x, y: abs(sd(x, y)) < h))

    def sdf(s, f, fill=False, w=None, to=None):
        """f(x, y) tren luoi 24 tra khoang cach co dau (am la trong), don vi luoi. Vien hoac to."""
        h = (w or s.w) / 2 - EPS
        g = lambda x, y: f(x / s.u, y / s.u) * s.u
        (to if to is not None else s.ink).append((lambda x, y: g(x, y) < 0) if fill else (lambda x, y: abs(g(x, y)) < h))

    # ---- to ----
    def fillpoly(s, *pts, to=None):
        q = [s.P(*p) for p in pts]
        (to if to is not None else s.ink).append(lambda x, y: _inpoly(x, y, q))

    def dot(s, cx, cy, r, to=None):
        X, Y = s.X(cx), s.X(cy)
        R = r * s.u
        (to if to is not None else s.ink).append(lambda x, y: math.hypot(x - X, y - Y) < R)

    # ---- ra anh ----
    @property
    def ink(s):
        return _Op(s.ops, 1)

    @property
    def cut(s):
        return _Op(s.ops, 0)

    def img(s):
        im = Image.new('1', (s.n, s.n), 1)
        px = im.load()
        for j in range(s.n):
            for i in range(s.n):
                x, y = i + 0.5, j + 0.5
                v = 1
                for k, f in s.ops:
                    if f(x, y):
                        v = 1 - k
                px[i, j] = v
        return im


def fillet(q, rs, closed):
    """Thay tung dinh bang cung tiep tuyen (roi rac 12 doan). Dinh dau, cuoi cua duong ho giu nguyen."""
    n = len(q)
    out = []
    for i in range(n):
        B = q[i]
        if not closed and i in (0, n - 1) or rs[i] <= 0:
            out.append(B)
            continue
        A, C = q[i - 1], q[(i + 1) % n]
        la, lc = math.hypot(A[0] - B[0], A[1] - B[1]), math.hypot(C[0] - B[0], C[1] - B[1])
        if la < EPS or lc < EPS:
            out.append(B)
            continue
        va = ((A[0] - B[0]) / la, (A[1] - B[1]) / la)
        vc = ((C[0] - B[0]) / lc, (C[1] - B[1]) / lc)
        th = math.acos(max(-1, min(1, va[0] * vc[0] + va[1] * vc[1])))
        if th > math.pi - 1e-3:
            out.append(B)
            continue
        t = min(rs[i] / math.tan(th / 2), la / 2, lc / 2)
        r = t * math.tan(th / 2)
        bx, by = va[0] + vc[0], va[1] + vc[1]
        bl = math.hypot(bx, by)
        d = r / math.sin(th / 2)
        O = (B[0] + bx / bl * d, B[1] + by / bl * d)
        T1 = (B[0] + va[0] * t, B[1] + va[1] * t)
        T2 = (B[0] + vc[0] * t, B[1] + vc[1] * t)
        a1 = math.atan2(T1[1] - O[1], T1[0] - O[0])
        a2 = math.atan2(T2[1] - O[1], T2[0] - O[0])
        da = (a2 - a1 + math.pi) % (2 * math.pi) - math.pi
        for k in range(13):
            a = a1 + da * k / 12
            out.append((O[0] + r * math.cos(a), O[1] + r * math.sin(a)))
    return out


def smin(a, b, k):
    """Hop mem hai khoang cach: khop lom thanh cung ban kinh khoang k."""
    h = max(k - abs(a - b), 0) / k
    return min(a, b) - h * h * k / 4


def sd_rrect(x, y, x0, y0, x1, y1, r):
    cx, cy, hx, hy = (x0 + x1) / 2, (y0 + y1) / 2, (x1 - x0) / 2, (y1 - y0) / 2
    qx, qy = abs(x - cx) - (hx - r), abs(y - cy) - (hy - r)
    return math.hypot(max(qx, 0), max(qy, 0)) + min(max(qx, qy), 0) - r


def crop(im):
    """Cat sat vung co muc (ky hieu nho ve tren khung vuong)."""
    from PIL import ImageOps
    b = ImageOps.invert(im.convert('L')).getbbox()
    return im.crop(b) if b else im


class _Op:
    def __init__(s, ops, k):
        s.ops, s.k = ops, k

    def append(s, f):
        s.ops.append((s.k, f))


def xam(im):
    """Gia xam tren net: cham xen ke 50%, neo theo goc icon."""
    out = im.copy()
    px = out.load()
    for y in range(im.height):
        for x in range(im.width):
            if px[x, y] == 0 and (x + y) % 2:
                px[x, y] = 1
    return out
