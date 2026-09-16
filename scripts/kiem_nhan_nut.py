#!/usr/bin/env python3
"""Kiem hang nhan nut tren mot anh chup tu simulator.

Ly do no ton tai: nhan nut bi be xuong hai dong thi chu tran ra khoi hop, va
loi do chi lo ra khi nhin man that. Bo kiem nay dem SO DAI CHU trong tung hop:
mot dai la mot dong, hai dai tro len la nhan bi be.

Hinh hoc lay tu BaseTheme::drawButtonHints: bon hop rong 106, cao 40, nam sat
day man, x = 38/154/268/384 tren panel rong 528 va 25/130/245/350 tren panel hep.

Dung:  python3 scripts/kiem_nhan_nut.py <anh.png> [ten-man]
Thoat 0 neu moi nhan nam gon mot dong, 1 neu co nhan bi be.
"""
import sys
from PIL import Image

RONG_HOP, CAO_HOP = 106, 40
X_RONG = [38, 154, 268, 384]   # panel >= 528 (X3)
X_HEP = [25, 130, 245, 350]    # panel 480 (X4 va cac board 800x480)
NGUONG_DEN = 128


def dai_chu(o, ty_le):
    """Cac dai hang CHU ben trong hop.

    Khong tru vien theo hinh hoc, vi vien hop thut vao trong o cat chu khong
    nam sat mep, va ngay 14/09 phep tru co dinh da truot qua no roi bao mot o
    RONG la co chu. Thay bang phep DEM: mot hang chi tinh la chu khi so diem
    muc vuot han hai duong vien doc. Vien day mot diem anh lo gic, tuc `ty_le`
    diem tren anh chup, nen hai vien cho khoang 2*ty_le diem.
    """
    w, h = o.size
    px = o.convert("L").load()
    nguong_hang = 4 * ty_le            # hon han hai vien doc
    co_chu = [sum(1 for x in range(w) if px[x, y] < NGUONG_DEN) > nguong_hang
              for y in range(h)]

    dai, dang_trong_dai = [], False
    for y, co in enumerate(co_chu):
        if co and not dang_trong_dai:
            dai.append([y, y]); dang_trong_dai = True
        elif co:
            dai[-1][1] = y
        else:
            dang_trong_dai = False
    # Gop cac dai gan nhau lai. Tieng Viet co dau nam TACH khoi than chu, nen
    # "Len" cho ba dai roi: dau mu, than chu, dau nang. Chung deu thuoc MOT dong.
    # Hai dong that cach nhau xa hon han: do tren man Cai dat, khe giua hai dong
    # cua "Trinh doc" rong gap nhieu lan khe giua dau va than chu.
    khe_toi_da = 3 * ty_le
    gop = []
    for d in dai:
        if gop and d[0] - gop[-1][1] <= khe_toi_da:
            gop[-1][1] = d[1]
        else:
            gop.append(d)
    # Dai mong hon hai diem anh lo gic la net vien hay rang cua, khong phai dong chu.
    return [d for d in gop if d[1] - d[0] >= 2 * ty_le]


def kiem(duong_anh, ten_man=""):
    im = Image.open(duong_anh)
    W, H = im.size
    ty_le = W // 528 if W % 528 == 0 else W // 480
    xs = X_RONG if W // ty_le >= 528 else X_HEP

    hong = False
    print(f"{ten_man or duong_anh}  (anh {W}x{H}, ty le {ty_le}x)")
    for i, x in enumerate(xs):
        hop = im.crop((x * ty_le, H - CAO_HOP * ty_le, (x + RONG_HOP) * ty_le, H))
        dai = dai_chu(hop, ty_le)
        if not dai:
            print(f"   nut {i + 1}: trong")
        elif len(dai) == 1:
            print(f"   nut {i + 1}: MOT DONG")
        else:
            hong = True
            print(f"   nut {i + 1}: BE {len(dai)} DONG  <-- nhan khong vua hop")
    return hong


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__); sys.exit(2)
    sys.exit(1 if kiem(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else "") else 0)
