#!/usr/bin/env python3
"""Sinh anh ngoi sao GHIM cho tab Yeu thich cua tenor/cross.

Chu may dat hinh 14/09/2026: ngoi sao co GOC BO TRON va CO FILL ben trong.

Cach lam goc tron: ve ngoi sao sac o do phan giai gap 16 lan, roi mo (erode roi
dilate) bang mot dia ban kinh r de bo tron goc loi, roi dong (dilate roi erode)
cung dia do de bo tron goc lom. Ha xuong co that bang nguong 50%.

Dinh dang ra: BW1 cua FreeInkUI, hang lien hang, bit cao truoc, bit bang 1 la muc.

    python3 scripts/sinh_ngoi_sao.py > src/components/NgoiSaoGhim.h
"""
import math
import sys

import numpy as np
from PIL import Image, ImageDraw

CO = 24          # be rong va cao that, diem anh
SIEU = 16        # he so sieu lay mau
BO_TRON = 0.04   # ban kinh bo goc, tinh theo be rong anh
NGOAI = 0.48     # ban kinh dinh ngoai, theo be rong
TRONG = 0.19    # ban kinh dinh trong; ty le nam canh cua ngoi sao nam canh


def dia(r: float) -> np.ndarray:
    n = int(math.ceil(r))
    y, x = np.ogrid[-n:n + 1, -n:n + 1]
    return (x * x + y * y) <= r * r


def gian(a: np.ndarray, k: np.ndarray) -> np.ndarray:
    ra = np.zeros_like(a)
    kn = k.shape[0] // 2
    ys, xs = np.nonzero(k)
    for y, x in zip(ys, xs):
        dy, dx = int(y) - kn, int(x) - kn
        ra |= np.roll(np.roll(a, dy, axis=0), dx, axis=1)
    return ra


def co(a: np.ndarray, k: np.ndarray) -> np.ndarray:
    return ~gian(~a, k)


def ve_sao(canh: int) -> np.ndarray:
    im = Image.new("1", (canh, canh), 0)
    d = ImageDraw.Draw(im)
    tam = canh / 2.0
    diem = []
    for i in range(10):
        goc = -math.pi / 2 + i * math.pi / 5
        r = (NGOAI if i % 2 == 0 else TRONG) * canh
        diem.append((tam + r * math.cos(goc), tam + r * math.sin(goc)))
    d.polygon(diem, fill=1)
    return np.array(im, dtype=bool)


def main() -> None:
    canh = CO * SIEU
    a = ve_sao(canh)
    k = dia(BO_TRON * canh)
    a = gian(co(a, k), k)   # mo: bo tron goc loi, tuc nam dinh nhon
    a = co(gian(a, k), k)   # dong: bo tron goc lom, tuc nam khe giua hai dinh

    nho = np.zeros((CO, CO), dtype=bool)
    for y in range(CO):
        for x in range(CO):
            o = a[y * SIEU:(y + 1) * SIEU, x * SIEU:(x + 1) * SIEU]
            nho[y, x] = o.mean() >= 0.5

    byte_moi_hang = (CO + 7) // 8
    byte = []
    for y in range(CO):
        for b in range(byte_moi_hang):
            v = 0
            for bit in range(8):
                x = b * 8 + bit
                if x < CO and nho[y, x]:
                    v |= 1 << (7 - bit)
            byte.append(v)

    ra = sys.stdout
    ra.write("// SINH RA bang scripts/sinh_ngoi_sao.py. DUNG SUA TAY.\n")
    ra.write("//\n")
    ra.write("
    ra.write("// co fill ben trong. Doi hinh thi sua tham so trong script roi chay lai.\n")
    ra.write("#pragma once\n\n")
    ra.write("#include <FreeInkUICore.h>\n\n")
    ra.write("#include <cstdint>\n\n")
    ra.write("namespace ngoisao {\n\n")
    ra.write(f"inline constexpr uint16_t CO = {CO};\n\n")
    ra.write("inline constexpr uint8_t DU_LIEU[] = {\n")
    for i in range(0, len(byte), 12):
        ra.write("    " + " ".join(f"0x{v:02X}," for v in byte[i:i + 12]) + "\n")
    ra.write("};\n\n")
    ra.write("// BW1: hang lien hang, bit cao truoc, bit bang 1 la muc.\n")
    ra.write("inline freeink::ui::BitmapRef ghim() {\n")
    ra.write("  freeink::ui::BitmapRef b;\n")
    ra.write("  b.data = DU_LIEU;\n")
    ra.write("  b.width = CO;\n")
    ra.write("  b.height = CO;\n")
    ra.write("  b.format = freeink::ui::BitmapFormat::BW1;\n")
    ra.write("  b.progmem = false;\n")
    ra.write("  return b;\n")
    ra.write("}\n\n")
    ra.write("}  // namespace ngoisao\n")


if __name__ == "__main__":
    main()
