"""Kiểm tài sản font CJK thử nghiệm trong `test/fonts-cjk/`.

Chạy: python3 test/fonts-cjk/verify_font.py

Kiểm những thứ mà một bản thay thế nhầm hoặc một manifest cũ sẽ làm hỏng:
  1. Tệp nguồn đúng là bản chính thức đã ghim (sha256), không phải font khác.
  2. Tệp OFL đi kèm khớp từng byte với bản đang phát hành ở
     `licenses/fonts/NotoSansSC-OFL.txt`.
  3. Font phủ đủ ký tự CJK mà preset `cjk` của fontconvert_sdcard.py cần, và
     các ký tự của câu fixture C1.
  4. Gói `.cpfont` thử đúng là bản đã ghim, và `fonts.json` khớp byte thật
     (tên tệp, kích thước, crc32) — đúng thứ máy đọc dùng để tải.
"""

from __future__ import annotations

import hashlib
import json
import sys
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent

FONT = HERE / "NotoSansSC[wght].ttf"
OFL = HERE / "NotoSansSC-OFL.txt"
SHIPPED_OFL = ROOT / "licenses" / "fonts" / "NotoSansSC-OFL.txt"

CPFONT_DIR = HERE / "cpfont"
MANIFEST = CPFONT_DIR / "fonts.json"

# Ghim nguồn chính thức: google/fonts main, ofl/notosanssc. FONT_SHA256 trùng
# SOURCE_SHA trong scripts/build_chinese_ui.py, tức đúng tệp firmware đang dùng.
FONT_SHA256 = "a3041811a78c361b1de50f953c805e0244951c21c5bd412f7232ef0d899af0da"
OFL_SHA256 = "1c05c68c34f9708415aada51f17e1b0092d2cea709bf4a94cd38114f9e73d7d9"

# Gói .cpfont thử: instance wght=400, preset "cjk", cỡ 12 và 18.
CPFONT_SHA256 = {
    "NotoSansSC_12.cpfont": "e6884aa3d99d917349c7b857e9c960770e7928e7cc03118a588e67bc2a039fca",
    "NotoSansSC_18.cpfont": "8381af43e27ab61c471c859c95465fb8c43faa9d8df3c8358061acfbf5d7e474",
}
MANIFEST_SHA256 = "f2a9cf6764d1696180d1f2cf5fb516741b965fc0cbb4b776743c85fc1f6f6eec"

# Preset "cjk" của lib/EpdFont/scripts/fontconvert_sdcard.py.
CJK_INTERVALS = [
    (0x3000, 0x303F),
    (0x3040, 0x309F),
    (0x30A0, 0x30FF),
    (0x4E00, 0x9FFF),
    (0xF900, 0xFAFF),
    (0xFF00, 0xFFEF),
]

# Khối CJK Unified Ideographs BMP gần đủ (đo được: 20.976/20.992). Ngưỡng thấp
# hơn một chút để không phụ thuộc đúng bản phát hành thượng nguồn.
MIN_IDEOGRAPHS = 20_900

# Ký tự bắt buộc vẽ được: câu fixture C1 + kana + dấu câu toàn phần.
REQUIRED = "夜色渐深，书页间的线索渐渐清晰ひらがなカタカナ。、"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def crc32(path: Path) -> int:
    """Giống compute_crc32 của scripts/generate-font-manifest.py (zlib, khớp
    esp_rom_crc32_le)."""
    value = 0
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(65536), b""):
            value = zlib.crc32(chunk, value)
    return value & 0xFFFFFFFF


def check_source(failures: list[str]) -> int:
    for path in (FONT, OFL, SHIPPED_OFL):
        if not path.is_file():
            failures.append(f"thiếu tệp: {path}")
    if failures:
        return 0

    if sha256(FONT) != FONT_SHA256:
        failures.append(f"{FONT.name}: sha256 khác bản đã ghim ({sha256(FONT)})")
    if sha256(OFL) != OFL_SHA256:
        failures.append(f"{OFL.name}: sha256 khác bản đã ghim ({sha256(OFL)})")
    if OFL.read_bytes() != SHIPPED_OFL.read_bytes():
        failures.append(f"{OFL.name}: khác bản đang phát hành {SHIPPED_OFL}")

    from fontTools.ttLib import TTFont

    cmap = TTFont(FONT, lazy=True).getBestCmap()
    codepoints = set(cmap)

    ideographs = sum(1 for cp in range(0x4E00, 0x9FFF + 1) if cp in codepoints)
    if ideographs < MIN_IDEOGRAPHS:
        failures.append(
            f"chỉ có {ideographs} ideograph trong U+4E00–9FFF, cần ≥ {MIN_IDEOGRAPHS}"
        )

    missing = [ch for ch in REQUIRED if ord(ch) not in codepoints]
    if missing:
        failures.append(f"thiếu glyph cho ký tự bắt buộc: {''.join(missing)}")

    for start, end in CJK_INTERVALS:
        covered = sum(1 for cp in range(start, end + 1) if cp in codepoints)
        if covered == 0:
            failures.append(f"khoảng U+{start:04X}–U+{end:04X} không có glyph nào")

    return ideographs


def check_pack(failures: list[str]) -> int:
    """Gói .cpfont thử và manifest của nó."""
    if not CPFONT_DIR.is_dir():
        failures.append(f"thiếu thư mục: {CPFONT_DIR}")
        return 0

    for name, expected in CPFONT_SHA256.items():
        path = CPFONT_DIR / name
        if not path.is_file():
            failures.append(f"thiếu tệp: {path}")
            continue
        actual = sha256(path)
        if actual != expected:
            failures.append(f"{name}: sha256 khác bản đã ghim ({actual})")

    if not MANIFEST.is_file():
        failures.append(f"thiếu manifest: {MANIFEST}")
        return 0
    actual_manifest = sha256(MANIFEST)
    if actual_manifest != MANIFEST_SHA256:
        failures.append(f"{MANIFEST.name}: sha256 khác bản đã ghim ({actual_manifest})")

    # Hợp đồng máy đọc dựa vào: mỗi mục trong manifest phải trỏ đúng byte thật.
    manifest = json.loads(MANIFEST.read_text())
    files = [entry for family in manifest.get("families", []) for entry in family["files"]]
    if not files:
        failures.append(f"{MANIFEST.name}: không có mục tệp nào")

    for entry in files:
        path = CPFONT_DIR / entry["name"]
        if not path.is_file():
            failures.append(f"{MANIFEST.name}: trỏ tới tệp không có: {entry['name']}")
            continue
        if entry["size"] != path.stat().st_size:
            failures.append(
                f"{entry['name']}: manifest ghi size={entry['size']}, thật "
                f"{path.stat().st_size}"
            )
        if entry["crc32"] != crc32(path):
            failures.append(
                f"{entry['name']}: manifest ghi crc32={entry['crc32']:#010x}, thật "
                f"{crc32(path):#010x}"
            )

    return len(files)


def main() -> int:
    failures: list[str] = []

    ideographs = check_source(failures)
    packed_files = check_pack(failures)

    if failures:
        for line in failures:
            print(f"FAIL {line}", file=sys.stderr)
        return 1

    print(f"OK {FONT.name}: sha256 khớp ghim, {ideographs} ideograph, "
          f"đủ {len(REQUIRED)} ký tự bắt buộc")
    print(f"OK {OFL.name}: sha256 khớp ghim và giống bản đang phát hành")
    print(f"OK {MANIFEST.name}: sha256 khớp ghim, {packed_files} mục khớp "
          f"size + crc32 với byte thật")
    print("Nhắc lại: tài sản này chỉ để thử, chưa nằm trong bộ phát hành.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
