"""Icon set for tenor/cross (round set, 04/10/2026), drawn by net.py.

Rounding rules (grid 24, radius measured at the stroke CENTRE):
- BO_LON 3: corners of big shapes (side 8 or more).
- BO_VUA 2: corners of small shapes, tab shoulders, arrow and chevron tips.
- BO_LOM 1.5: concave joints (tab foot, ribbon notch, book spine), so the inside of the stroke is round too.
Stroke ends and joints are always round. Grid 24, drawing area 20 (margin 2).
"""
import math
from net import G, smin, sd_rrect

BO_LON, BO_VUA, BO_LOM = 3, 2, 1.5
H = {}


def reg(name, vi):
    def d(f):
        H[name] = (vi, f)
        return f
    return d


@reg('gan_day', 'Gan day: dong ho')
def _(g):
    g.circle(12, 12, 9.5)
    g.rpoly((12, 7), (12, 12), (15.5, 14.5), r=BO_LOM)


def heart_pts(cx=12, top=4, tip=21.2, half=10, lob=5.4):
    """Vien tim: 2 thuy tron, 2 canh thang tiep tuyen xuong mui. Tra (diem, ban kinh bo tung dinh)."""
    lx, rx = cx - half + lob, cx + half - lob
    cy = top + lob
    T = (cx, tip)

    def tang(ox, side):                      # diem tiep tuyen tu mui toi vong thuy, phia ngoai
        dx, dy = T[0] - ox, T[1] - cy
        d = math.hypot(dx, dy)
        a = math.atan2(dy, dx) + side * math.acos(lob / d)
        return a
    aL = tang(lx, 1)
    aR = tang(rx, -1)
    aCL = -math.acos((cx - lx) / lob)        # cho thuy trai cham truc giua (khe tren)
    pts, rs = [], []
    # thuy trai: tu tiep tuyen duoi, vong qua trai, len dinh, toi khe
    a1 = aCL
    while a1 < aL:
        a1 += 2 * math.pi
    for k in range(25):
        a = aL + (a1 - aL) * k / 24
        pts.append((lx + lob * math.cos(a), cy + lob * math.sin(a)))
        rs.append(0)
    rs[-1] = 1.6                               # khe giua bo nhe
    aCR = math.pi + math.acos((cx - lx) / lob)
    b1 = aR
    while b1 < aCR:
        b1 += 2 * math.pi
    for k in range(1, 25):
        a = aCR + (b1 - aCR) * k / 24
        pts.append((rx + lob * math.cos(a), cy + lob * math.sin(a)))
        rs.append(0)
    pts.append(T)
    rs.append(4)                               # mui tim tron
    return pts, rs


@reg('yeu_tim', 'Yeu thich: tim')
def _(g):
    g.snap = False
    p, r = heart_pts()
    g.rpoly(*p, r=r, closed=True)
    g.snap = True


@reg('thu_muc_hop', 'Thu muc C: tui lien tab (hop mem)')
def _(g):
    def f(x, y):
        body = sd_rrect(x, y, 3, 7.5, 21, 19.5, BO_LON)
        tab = sd_rrect(x, y, 3, 4, 12, 10, BO_VUA)
        return smin(body, tab, 2.5)
    g.sdf(f)


@reg('cai_dat', 'Cai dat: 2 thanh truot, num o dau')
def _(g):
    g.line((3, 7), (12, 7))
    g.circle(18, 7, 3)
    g.line((12, 17), (21, 17))
    g.circle(6, 17, 3)


@reg('thong_ke', 'Thong ke: 3 cot')
def _(g):
    g.line((5, 20), (5, 14))
    g.line((12, 20), (12, 9))
    g.line((19, 20), (19, 4))


@reg('vi_tri', 'Vi tri: ruy bang')
def _(g):
    g.rpoly((6.5, 3), (17.5, 3), (17.5, 21), (12, 16.5), (6.5, 21),
            r=[BO_VUA, BO_VUA, BO_LOM, BO_LOM, BO_LOM], closed=True)


@reg('doc', 'Doc: sach mo, hai trang cong doi xung')
def _(g):
    """Open book: each page's top edge flat, then curving down into the spine, the base folding up at the
    spine. Only the left page is drawn; the right one is its mirror, pixel for pixel, so the two pages are
    equal at every size and stroke (a stroke centred on the spine cannot be both odd-wide and symmetric:
    the spine takes the left stroke's half, mirrored, 4 px at 40 px)."""
    page = []
    g.rpoly((12, 8.5), (8.5, 5), (3, 5), (3, 17.5), (9, 17.5), (12, 20.5),
            r=[BO_LON, BO_LON, BO_VUA, BO_VUA, BO_LON, BO_LON], closed=True, to=page)
    half = g.n / 2
    g.ink.append(lambda x, y: any(f(x if x < half else g.n - x, y) for f in page))


@reg('cong_cu', 'Cong cu A: luoi 4 o bo')
def _(g):
    # At stroke 4 the cells shrink so the gap between them stays open (7.5 wide leaves 1 px).
    s = 7.5 if g.w < 4 else 6.5
    for x, y in ((3, 3), (21 - s, 3), (3, 21 - s), (21 - s, 21 - s)):
        g.rrect(x, y, x + s, y + s, BO_VUA)


@reg('tep_sach', 'Tep sach: sach dong')
def _(g):
    g.rpoly((7.5, 21.5), (19, 21.5), (19, 2.5), (5, 2.5), (5, 19), r=[0, BO_VUA, BO_VUA, BO_LON, 0])
    g.arc(7.5, 19, 2.5, 90, 270)
    g.line((7.5, 16.5), (19, 16.5))


def page(g, x0=5, y0=2.5, x1=19, y1=21.5, fold=5.5):
    g.rpoly((x1 - fold, y0), (x0, y0), (x0, y1), (x1, y1), (x1, y0 + fold),
            r=[BO_LOM, BO_LON, BO_LON, BO_LON, BO_LOM], closed=True)
    g.rpoly((x1 - fold, y0 + 0.5), (x1 - fold, y0 + fold), (x1 - 0.5, y0 + fold), r=BO_VUA)


@reg('tep_chu', 'Tep chu: trang co dong')
def _(g):
    page(g)
    g.line((9, 13), (15, 13))
    g.line((9, 17), (15, 17))


@reg('tep_anh', 'Tep anh')
def _(g):
    g.rrect(3, 4, 21, 20, BO_LON)
    g.circle(8.5, 9.5, 1.6)
    g.rpoly((3.5, 18.5), (10, 12), (15.5, 17.5), (21, 13), r=BO_VUA)


@reg('tep', 'Tep khac: trang gap goc')
def _(g):
    page(g)


@reg('wifi', 'Ket noi mang: Wi-Fi')
def _(g):
    g.arc(12, 20, 15.5, 222, 318)
    g.arc(12, 20, 10.5, 225, 315)
    g.arc(12, 20, 5.5, 230, 310)
    g.dot(12, 20, 1.9)


@reg('bluetooth', 'Remote Bluetooth da noi (vong 5b, founder duyet 04/10)')
def _(g):
    g.poly((7, 7), (17, 17), (12, 22), (12, 2), (17, 7), (7, 17))


@reg('bluetooth_hong', 'Remote Bluetooth rot hoac noi hong: hinh tren, mot gach cheo')
def _(g):
    g.poly((7, 7), (17, 17), (12, 22), (12, 2), (17, 7), (7, 17))
    g.line((3, 3), (21, 21))


@reg('chon', 'Gia tri dang dung trong danh sach chon: dau tick')
def _(g):
    g.rpoly((5, 12.5), (10, 17.5), (19, 6.5), r=BO_VUA)


@reg('thu_vien', 'Calibre: ke sach')
def _(g):
    g.line((5, 3.5), (5, 20.5))
    g.line((10, 3.5), (10, 20.5))
    g.snap = False
    g.rpoly((14, 6), (17.6, 5), (21.6, 19.5), (18, 20.5), r=BO_LOM, closed=True)
    g.snap = True


@reg('diem_phat', 'Tao diem phat')
def _(g):
    g.dot(12, 12, 2.2)
    g.arc(12, 12, 5.5, 135, 225)
    g.arc(12, 12, 5.5, 315, 45)
    g.arc(12, 12, 10, 140, 220)
    g.arc(12, 12, 10, 320, 40)


def khay(g):
    g.rpoly((3.5, 14), (3.5, 20.5), (20.5, 20.5), (20.5, 14), r=BO_LON)


@reg('tai_xuong', 'Lay ban may chu')
def _(g):
    g.line((12, 3.5), (12, 15))
    g.rpoly((7, 10), (12, 15), (17, 10), r=BO_LOM)
    khay(g)


@reg('tai_len', 'Day ban may len')
def _(g):
    g.line((12, 3.5), (12, 15))
    g.rpoly((7, 8.5), (12, 3.5), (17, 8.5), r=BO_LOM)
    khay(g)


@reg('tim', 'Tim kiem: kinh lup')
def _(g):
    g.circle(10.5, 10.5, 7)
    g.line((15.5, 15.5), (20.5, 20.5))


@reg('muc_luc', 'Muc luc')
def _(g):
    for y in (6, 12, 18):
        g.dot(4.5, y, 1.9)
        g.line((9.5, y), (20.5, y))


@reg('chu', 'Chu: Aa')
def _(g):
    g.rpoly((2.5, 19.5), (7.25, 4.5), (12, 19.5), r=BO_VUA)
    g.line((4.3, 14), (10.2, 14))
    g.circle(17.9, 15.6, 3.3)
    g.line((21.2, 12.3), (21.2, 19.5))


@reg('them', 'Them: 3 cham')
def _(g):
    for x in (5, 12, 19):
        g.dot(x, 12, 2)


@reg('lui', 'Buoc lui')
def _(g):
    g.rpoly((15, 4.5), (7.5, 12), (15, 19.5), r=BO_VUA)


@reg('toi', 'Buoc toi')
def _(g):
    g.rpoly((9, 4.5), (16.5, 12), (9, 19.5), r=BO_VUA)


@reg('dau_tich', 'Dau tich')
def _(g):
    g.rpoly((4.5, 12.5), (9.5, 17.5), (19.5, 6.5), r=BO_VUA)


# Text panel values, drawn as a row of icons (reader toolbar, Text): alignment, line spacing, drop cap.
def _dong(g, *spans, y0=5, buoc=5):
    for i, (a, b) in enumerate(spans):
        g.line((a, y0 + i * buoc), (b, y0 + i * buoc))


@reg('can_deu', 'Can deu hai ben')
def _(g):
    _dong(g, (3, 21), (3, 21), (3, 21), (3, 14))


@reg('can_trai', 'Can trai')
def _(g):
    _dong(g, (3, 21), (3, 15), (3, 19), (3, 12))


@reg('can_giua', 'Can giua')
def _(g):
    _dong(g, (3, 21), (6, 18), (4, 20), (8, 16))


@reg('can_phai', 'Can phai')
def _(g):
    _dong(g, (3, 21), (9, 21), (5, 21), (12, 21))


@reg('can_sach', 'Kieu sach: deu hai ben, thut dau dong')
def _(g):
    _dong(g, (8, 21), (3, 21), (3, 21), (3, 14))


def _gian(g, buoc):
    for k in (-1, 0, 1):
        g.line((4, 12 + k * buoc), (20, 12 + k * buoc))


@reg('gian_1', 'Gian dong sieu hep')
def _(g):
    _gian(g, 3)


@reg('gian_2', 'Gian dong hep')
def _(g):
    _gian(g, 4.5)


@reg('gian_3', 'Gian dong mac dinh')
def _(g):
    _gian(g, 6)


@reg('gian_4', 'Gian dong rong')
def _(g):
    _gian(g, 7.5)


@reg('gian_5', 'Gian dong sieu rong')
def _(g):
    _gian(g, 9)


@reg('hoa_tat', 'Chu lon dau chuong: tat')
def _(g):
    _dong(g, (3, 21), (3, 21), (3, 21), (3, 21))


@reg('hoa_vua', 'Chu lon dau chuong: hai dong')
def _(g):
    g.rrect(3, 3, 9.5, 11.5, r=BO_VUA, fill=True)
    _dong(g, (13, 21), (13, 21), (3, 21), (3, 21))


@reg('hoa_lon', 'Chu lon dau chuong: ba dong')
def _(g):
    g.rrect(3, 3, 11, 16.5, r=BO_VUA, fill=True)
    _dong(g, (14.5, 21), (14.5, 21), (14.5, 21), (3, 21))


@reg('trang_khuyet', 'Mat trang (man ngu), sung tron')
def _(g):
    k = 2.4                                    # co vao k roi no ra k: hai sung thanh cung ban kinh k

    def f(x, y):
        d1 = math.hypot(x - 12, y - 12) - 9.5
        d2 = math.hypot(x - 16.5, y - 7.5) - 7.5
        return max(d1 + k, -(d2 - k)) - k
    g.sdf(f)
