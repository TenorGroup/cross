#!/usr/bin/env python3
"""Write icon headers from the approved icon57 PNGs and the remaining shapes in hinh.py.

    python3 scripts/icons/sinh.py            # rewrite the headers
    python3 scripts/icons/sinh.py --check    # exit 1 when a header on disk differs from what this makes

Format of freeink::Icon (see freeink-sdk/libs/assets/Icons/include/Icon.h): 1 bit, every row padded
to a multiple of 8 bits, high bit first, bit 0 = ink. The last field is the optical centre row, the
mean row of the ink. The arrays are `inline constexpr`: one copy in the image however many .cpp
files include the header (a `static const` array is copied into each one that uses it).

Approved 40 px cards use grey regular PNGs (made by xam.py) and selected stroke-3 PNGs. Other 40 px shapes
keep their existing selected artwork (stroke 4); their grey regular art is cut out of it by xam.py.
Smaller sizes are unchanged.

MoonIcon.h is the one asset in the pre-rotated layout of scripts/convert_icon.py (turned 90 degrees
counter-clockwise, white = 1), drawn by SleepActivity through renderer.drawImage.
"""
import os
import sys

from PIL import Image, ImageChops

from net import G
import hinh
import song
import xam

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, 'src', 'components', 'icons')
MOON = os.path.join(ROOT, 'src', 'images', 'MoonIcon.h')
ICON57 = os.path.join(ROOT, 'scripts', 'icons', 'icon57', 'png')
ICON57_NAMES = (
    'gan_day', 'thu_muc_hop', 'yeu_tim', 'thong_ke', 'cai_dat', 'hien_thi_b', 'trang_khuyet',
    'doc', 'dieu_khien', 'cu_chi', 'he_thong_b', 'thiet_bi', 'ban_phim', 'them',
    'thanh_trang_thai', 'ngon_ngu', 'thong_tin', 'font', 'co_chu', 'bo_cuc', 'kieu')

# (file, size, stroke, [(c name, shape in hinh.py) or (c name, shape, own stroke)])
# The tab icons come twice: baked grey, and the selected tab's `_bold` bitmap.
SETS = [
    ('tenorHomeTabIcons.h', 40, 3, [
        ('tenor_home_recent', 'gan_day'), ('tenor_home_folder', 'thu_muc_hop'),
        ('tenor_home_stats', 'thong_ke'), ('tenor_home_settings', 'cai_dat'),
        ('tenor_home_favorites', 'yeu_tim'),
        ('tenor_home_recent_bold', 'gan_day', 4), ('tenor_home_folder_bold', 'thu_muc_hop', 4),
        ('tenor_home_stats_bold', 'thong_ke', 4), ('tenor_home_settings_bold', 'cai_dat', 4),
        ('tenor_home_favorites_bold', 'yeu_tim', 4)]),
    ('tenorReaderTabIcons.h', 40, 3, [
        ('tenor_reader_position', 'vi_tri'), ('tenor_reader_reading', 'doc'),
        ('tenor_reader_tools', 'cong_cu'),
        ('tenor_reader_position_bold', 'vi_tri', 4), ('tenor_reader_reading_bold', 'doc', 4),
        ('tenor_reader_tools_bold', 'cong_cu', 4)]),
    # The tabs of the button boards' toolbar reader menu: its own glyphs (list, Aa, dots) at the tab size.
    ('readerToolbarTabIcons.h', 40, 3, [
        ('reader_tab_contents', 'muc_luc'), ('reader_tab_text', 'chu'), ('reader_tab_more', 'them'),
        ('reader_tab_contents_bold', 'muc_luc', 4), ('reader_tab_text_bold', 'chu', 4),
        ('reader_tab_more_bold', 'them', 4)]),
    ('tenorTextTabIcons.h', 40, 3, [
        (f'tenor_text_{shape}{suffix}', shape)
        for suffix in ('', '_bold') for shape in ('font', 'co_chu', 'bo_cuc', 'kieu')]),
    ('tenorSettingsIcons.h', 40, 3, [
        (f'tenor_settings_{shape}{suffix}', shape)
        for suffix in ('', '_bold') for shape in (
            'hien_thi_b', 'trang_khuyet', 'dieu_khien', 'cu_chi', 'he_thong_b',
            'thiet_bi', 'ban_phim', 'thanh_trang_thai', 'ngon_ngu', 'thong_tin')]),
    ('tenorRowIcons.h', 32, 2, [
        ('folder', 'thu_muc_hop'), ('file_text', 'tep_chu'), ('image', 'tep_anh'), ('book', 'tep_sach'),
        ('file', 'tep'), ('wifi', 'wifi'), ('library', 'thu_vien'), ('radio_tower', 'diem_phat'),
        ('bookmark', 'vi_tri'), ('recent', 'gan_day')]),
    ('tenorTransferIcons.h', 24, 2, [('download', 'tai_xuong'), ('upload', 'tai_len')]),
    # The status strip at the top of the touch shell: shown only while the radio is on.
    ('tenorStatusIcons.h', 18, 2, [('status_wifi', 'wifi'), ('status_bluetooth', 'bluetooth'),
                                   ('status_bluetooth_lost', 'bluetooth_hong')]),
    # The mark of the value in use at the end of a row in a list of choices (touch shell).
    ('tenorRowMarks.h', 24, 2, [('row_chosen', 'chon')]),
    ('search32.h', 32, 2, [('search', 'tim')]),
    ('readerToolbarIcons.h', 24, 2, [
        ('reader_contents', 'muc_luc'), ('reader_text', 'chu'), ('reader_more', 'them'),
        ('reader_back', 'lui'), ('reader_next', 'toi'), ('reader_tick', 'dau_tich'),
        ('reader_align_justify', 'can_deu'), ('reader_align_left', 'can_trai'), ('reader_align_center', 'can_giua'),
        ('reader_align_right', 'can_phai'), ('reader_align_book', 'can_sach'),
        ('reader_spacing_1', 'gian_1'), ('reader_spacing_2', 'gian_2'), ('reader_spacing_3', 'gian_3'),
        ('reader_spacing_4', 'gian_4'), ('reader_spacing_5', 'gian_5'),
        ('reader_dropcap_off', 'hoa_tat'), ('reader_dropcap_default', 'hoa_vua'), ('reader_dropcap_large', 'hoa_lon')]),
]


def draw(shape, n, w, bold=False):
    if n == 40 and shape in ICON57_NAMES:
        suffix = 'chon' if bold else 'xam'
        with Image.open(os.path.join(ICON57, f'{shape}-{suffix}.png')) as image:
            assert image.mode == '1' and image.size == (40, 40), (shape, image.mode, image.size)
            left, top, right, bottom = ImageChops.invert(image.convert('L')).getbbox()
            assert left - (40 - right) in (-1, 0) and top - (40 - bottom) in (-1, 0), (shape, suffix)
            return image.copy()
    if n == 40 and not bold:
        # Grey card of an older shape: its selected art (stroke 4) with 2 lanes of every stroke taken away.
        return xam.grey_image(shape, draw(shape, 40, 4, True), passes=2)
    g = G(n, w)
    hinh.H[shape][1](g)
    return g.img()


def pack(im):
    """Rows of bytes, high bit first, 1 = leave the pixel, 0 = ink; plus the optical centre row."""
    n = im.width
    px = im.load()
    data, sum_y, count = [], 0, 0
    for y in range(im.height):
        for xb in range(0, n, 8):
            byte = 0
            for b in range(8):
                x = xb + b
                white = 1
                if x < n and px[x, y] == 0:
                    white = 0
                    sum_y += y
                    count += 1
                byte |= white << (7 - b)
            data.append(byte)
    return data, (round(sum_y / count) if count else n // 2)


def wrap(values, per_line, close):
    lines = []
    for i in range(0, len(values), per_line):
        lines.append('    ' + ', '.join(f'0x{b:02X}' for b in values[i:i + per_line]))
    return ',\n'.join(lines) + close


def header(name, n, w, items):
    out = ['#pragma once', '', '#include "Icon.h"', '',
           '// Generated by scripts/icons/sinh.py from scripts/icons/hinh.py. Do not edit.',
           f'// Size: {n}px, stroke {w} unless the name ends in _bold (stroke 4). Icons: {len(items)}.', '']
    if n == 40:
        out[4] = '// Generated by scripts/icons/sinh.py from scripts/icons/icon57/png and hinh.py. Do not edit.'
        out[5] = f'// Size: 40px, regular is baked grey; _bold is selected. Icons: {len(items)}.'
    for cname, shape, *own in items:
        data, cy = pack(draw(shape, n, own[0] if own else w, cname.endswith('_bold')))
        arr = f'icon_{cname}_{n}_bits'
        out.append(f'// {cname}  ({shape})')
        out.append(f'inline constexpr uint8_t {arr}[] = {{')
        out.append(wrap(data, 19, '};'))
        out.append(f'inline constexpr freeink::Icon icon_{cname}_{n} = {{{n}, {n}, {cy}, {arr}}};')
        out.append('')
    return '\n'.join(out).rstrip('\n') + '\n'


def signal_header():
    """The marks of the Wi-Fi list (song.py): not square, so they pack here and not through header()."""
    out = ['#pragma once', '', '#include "Icon.h"', '',
           '// Generated by scripts/icons/sinh.py from scripts/icons/song.py. Do not edit.',
           '// Wi-Fi list marks: wifi_signal_0..4 are 22x18 (0 = all grey), wifi_lock is 12x16. Stroke 2, grey is baked.', '']
    for name, image in [(f'wifi_signal_{k}', song.wave(k)) for k in range(5)] + [('wifi_lock', song.lock())]:
        data, cy = pack(image)
        out.append(f'inline constexpr uint8_t icon_{name}_bits[] = {{')
        out.append(wrap(data, 19, '};'))
        out.append(f'inline constexpr freeink::Icon icon_{name} = {{{image.width}, {image.height}, {cy}, icon_{name}_bits}};')
        out.append('')
    return '\n'.join(out).rstrip('\n') + '\n'


def moon():
    im = draw('trang_khuyet', 48, 3).rotate(90, expand=True)
    data, _ = pack(im)
    return ('#pragma once\n#include <cstdint>\n\n// Image dimensions: 48x48\n#define MOONICON_WIDTH 48\n'
            '#define MOONICON_HEIGHT 48\n\nstatic const uint8_t MoonIcon[] = {\n' + wrap(data, 18, '};') + '\n')


def outputs():
    res = {os.path.join(OUT, f): header(f, n, w, items) for f, n, w, items in SETS}
    res[os.path.join(OUT, 'tenorSignalIcons.h')] = signal_header()
    res[MOON] = moon()
    return res


def main():
    check = '--check' in sys.argv[1:]
    bad = 0
    for path, text in outputs().items():
        have = open(path).read() if os.path.exists(path) else None
        if check:
            if have != text:
                print('differs:', os.path.relpath(path, ROOT))
                bad += 1
        else:
            open(path, 'w').write(text)
            print('wrote', os.path.relpath(path, ROOT))
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
