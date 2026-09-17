"""Điền maxInkTop cho các tệp .cpfont đã phát hành, không cần TTF nguồn.

Vì sao cần: bản v1.0.2 phát hành trước khi trường ink-top ra đời, nên đuôi dự trữ
của mỗi mục style toàn số 0 và firmware đọc ra 0 ("chưa biết"). Khi chưa biết, trình
đọc giữ công thức cũ `lề + 1` cho dòng đầu, và dấu tiếng Việt vượt quá ascender bị
cắt ở mép trên (đo được: dòng đầu mất 3 px, `!! Outside range (…, -1)`).

Cách làm: mỗi style đã có sẵn bitmap và trường `top` của từng glyph trong tệp, nên
tính lại được đúng giá trị mà `fontconvert_sdcard.py` ghi khi dựng pack:
    maxInkTop = max(glyph.top) trên các glyph có width và height > 0
rồi ghi vào 2 byte cuối phần dự trữ của mục TOC (vẫn 32 byte, version vẫn 4).

Chỉ đuôi dự trữ đổi; mọi byte khác của tệp phải y nguyên (kiểm bằng so sánh).
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

HEADER_SIZE = 32
STYLE_TOC_ENTRY_SIZE = 32
STYLE_TOC_FORMAT = "<B3xIIBhhHHBBBIh2x"
GLYPH_STRUCT_FORMAT = "<BBHhhH2xI"
INTERVAL_STRUCT_FORMAT = "<III"
INK_TOP_OFFSET_IN_ENTRY = 28
MAGIC = b"CPFONT\x00\x00"
VERSION = 4


def patch_one(path: Path, ghi: bool = False) -> dict:
    data = bytearray(path.read_bytes())
    magic, version, _flags, style_count = struct.unpack_from("<8sHHB", data, 0)
    if magic != MAGIC or version != VERSION:
        return {"tep": path.name, "ket_qua": "bo_qua", "ly_do": f"magic/version {magic!r}/{version}"}
    ket = {"tep": path.name, "styles": []}
    for i in range(style_count):
        base = HEADER_SIZE + i * STYLE_TOC_ENTRY_SIZE
        (style_id, interval_count, glyph_count, _advance_y, _ascender, _descender, _kern_l, _kern_r,
         _kern_l_count, _kern_r_count, _lig_count, data_offset, cu) = struct.unpack_from(STYLE_TOC_FORMAT, data,
                                                                                         base)
        glyph_base = data_offset + interval_count * struct.calcsize(INTERVAL_STRUCT_FORMAT)
        ink_top = 0
        for g in range(glyph_count):
            off = glyph_base + g * struct.calcsize(GLYPH_STRUCT_FORMAT)
            width, height, _advance_x, _left, top, _data_len, _data_off = struct.unpack_from(
                GLYPH_STRUCT_FORMAT, data, off)
            if width and height and top > ink_top:
                ink_top = top
        if ink_top > 32767:
            return {"tep": path.name, "ket_qua": "loi", "ly_do": f"maxInkTop {ink_top} vượt int16"}
        if ghi and cu != ink_top:
            struct.pack_into("<h", data, base + INK_TOP_OFFSET_IN_ENTRY, ink_top)
        ket["styles"].append({"style": style_id, "cu": cu, "moi": ink_top, "glyphs": glyph_count})
    if ghi and any(s["cu"] != s["moi"] for s in ket["styles"]):
        path.write_bytes(bytes(data))
        ket["da_ghi"] = True
    return ket


def main() -> int:
    p = argparse.ArgumentParser(description="Điền maxInkTop cho tệp/tệp .cpfont")
    p.add_argument("paths", nargs="+", type=Path)
    p.add_argument("--ghi", action="store_true", help="ghi thật; thiếu cờ này chỉ đọc và báo")
    a = p.parse_args()
    tep = []
    for target in a.paths:
        tep.extend(sorted(target.rglob("*.cpfont")) if target.is_dir() else [target])
    if not tep:
        print("không tìm thấy tệp .cpfont nào", file=sys.stderr)
        return 1
    thieu = 0
    for t in tep:
        kq = patch_one(t, a.ghi)
        if kq.get("ket_qua"):
            print(f"{t.name}: {kq['ket_qua']} ({kq.get('ly_do','')})")
            continue
        for s in kq["styles"]:
            if s["moi"] == 0:
                thieu += 1
        print(f"{t.name}: " + ", ".join(f"style{s['style']} {s['cu']}->{s['moi']} ({s['glyphs']} glyph)"
                                        for s in kq["styles"]) + (" [đã ghi]" if kq.get("da_ghi") else ""))
    print(f"tổng {len(tep)} tệp; {thieu} style không có mực trên đường cơ sở (giữ 0)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
