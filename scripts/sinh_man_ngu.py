#!/usr/bin/env python3
"""Encode the fallback sleep art: python scripts/sinh_man_ngu.py input.png > src/components/ManNguTenor.h.

The input is the 528x792 art in the panel's four levels (0, 85, 170, 255). The output is the
frame the X3 shows when it has no absolute gray planes (the UC8253 X3): each gray level dotted
into black and white with the renderer's own patterns (GfxRenderer::fillRectDither: dark gray
black where x + y is even, light gray black where x and y are both even), laid out as the
portrait framebuffer, zlib compressed. The firmware inflates it straight into the framebuffer.
"""
import argparse
import sys
import zlib

from PIL import Image

RONG = 528  # logical portrait width
CAO = 792  # logical portrait height
PANEL_RONG = CAO  # the panel is landscape: logical (x, y) lands on panel (y, RONG - 1 - x)
HANG_BYTE = PANEL_RONG // 8


def den(muc: int, x: int, y: int) -> bool:
    if muc == 0:
        return True
    if muc == 1:
        return (x + y) % 2 == 0
    if muc == 2:
        return x % 2 == 0 and y % 2 == 0
    return False


def main() -> None:
    parser = argparse.ArgumentParser(description="Encode a 528x792 four-level fallback sleep image")
    parser.add_argument("source", help="Path to the input image")
    args = parser.parse_args()
    im = Image.open(args.source).convert("L")
    if im.size != (RONG, CAO):
        sys.exit(f"anh phai la {RONG}x{CAO}, dang la {im.size}")

    # Quy ve 4 muc cua panel; diem nao ngoai bon muc thi dung lai, dung de lech am tham.
    khung = bytearray(b"\xff" * (HANG_BYTE * RONG))  # 1 = trang
    diem = im.load()
    for y in range(CAO):
        for x in range(RONG):
            p = diem[x, y]
            muc = round(p / 85)
            if muc * 85 != p:
                sys.exit(f"anh co diem ngoai bon muc cua panel: {p}")
            if den(muc, x, y):
                px, py = y, RONG - 1 - x
                khung[py * HANG_BYTE + px // 8] &= ~(0x80 >> (px % 8)) & 0xFF
    nen = zlib.compress(bytes(khung), 9)

    ra = sys.stdout
    ra.write("// SINH RA bang scripts/sinh_man_ngu.py. DUNG SUA TAY.\n")
    ra.write("//\n")
    ra.write("// Man ngu Tenor du phong cho X3 khong co mat xam tuyet doi (X3 UC8253): mot khung den\n")
    ra.write("// trang, muc xam da cham san theo mau cua fillRectDither, xep dung bo dem khung doc,\n")
    ra.write(f"// nen zlib. Tho {len(khung)} byte; ban nen nay ton {len(nen)} byte.\n")
    ra.write("#pragma once\n\n#include <cstdint>\n\nnamespace mannogu {\n\n")
    ra.write("inline constexpr uint8_t KHUNG[] = {\n")
    for i in range(0, len(nen), 16):
        ra.write("    " + " ".join(f"0x{v:02x}," for v in nen[i:i + 16]) + "\n")
    ra.write("};\n\n")
    ra.write("}  // namespace mannogu\n")


if __name__ == "__main__":
    main()
