#!/usr/bin/env python3
"""Encode a sleep BMP: python scripts/sinh_man_ngu.py input.bmp > output.h."""
import sys
import argparse

from PIL import Image

RONG = 528
CAO = 792


def main() -> None:
    parser = argparse.ArgumentParser(description="Encode a 528x792 four-level greyscale sleep image")
    parser.add_argument("source", help="Path to the input BMP")
    args = parser.parse_args()
    im = Image.open(args.source).convert("L")
    if im.size != (RONG, CAO):
        sys.exit(f"anh phai la {RONG}x{CAO}, dang la {im.size}")

    # Quy ve 4 muc cua panel. An pham da dung dung bon muc nay nen phep quy nay khong
    # lam mat gi; kiem lai de neu ai do doi an pham sang thang khac thi vo ngay.
    muc = []
    for p in im.getdata():
        m = round(p / 85)
        if m * 85 != p:
            sys.exit(f"anh co diem ngoai bon muc cua panel: {p}")
        muc.append(m)

    doan = []
    truoc, dem = muc[0], 1
    for v in muc[1:]:
        if v == truoc and dem < 255:
            dem += 1
        else:
            doan.append((dem, truoc))
            truoc, dem = v, 1
    doan.append((dem, truoc))

    byte = []
    for dem, v in doan:
        byte.append(dem)
        byte.append(v)

    ra = sys.stdout
    ra.write("// SINH RA bang scripts/sinh_man_ngu.py. DUNG SUA TAY.\n")
    ra.write("//\n")
    ra.write("// Man ngu mac dinh cua tenor/cross. Nguon la an pham branding dung 12/09/2026,\n")
    ra.write("
    ra.write("//\n")
    ra.write("// Nen RLE: moi doan hai byte, byte dau la SO DIEM (1..255), byte sau la MUC (0..3),\n")
    ra.write("// muc 0 den, 3 trang. Diem chay lien tuc theo hang, tu trai sang phai, tu tren xuong.\n")
    ra.write(f"// De tho 2 bit moi diem ton {RONG * CAO // 4} byte; ban nen nay ton {len(byte)} byte.\n")
    ra.write("#pragma once\n\n#include <cstdint>\n\nnamespace mannogu {\n\n")
    ra.write(f"inline constexpr int RONG = {RONG};\ninline constexpr int CAO = {CAO};\n\n")
    ra.write(f"inline constexpr uint8_t DU_LIEU[] = {{\n")
    for i in range(0, len(byte), 16):
        ra.write("    " + " ".join(f"{v}," for v in byte[i:i + 16]) + "\n")
    ra.write("};\n\n")
    ra.write("}  // namespace mannogu\n")


if __name__ == "__main__":
    main()
