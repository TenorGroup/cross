#!/usr/bin/env python3
"""Bake the lines of abuse of the tenor/ugly shell: python3 scripts/ugly/gen_quips.py [out-dir]

Reads scripts/ugly/quips.csv (key, group, condition, vi, en, args) and writes src/shells/ugly/UglyQuips.h:
the Vietnamese and the English lines, each language one zlib block of \\0-ended lines in table order (the
Chinese interface uses the English block); where every 32nd line starts in each block, so a lookup
inflates only as far as it needs; and the slots: which lines an event says, keyed as the firmware keys it.

Keys: a page id for opening a notebook page; for the rest a 16-bit hash of the Vietnamese words the
firmware also has (a settings group's name, "row label=value" for a value picked), see quipKey().
A line whose condition the firmware cannot tell apart yet (a value set on a screen of tenor/cross, a range of
numbers) is left out of the blocks, and so is a line of an event the firmware does not say yet (SAID); `-v`
lists them.
"""
import csv
import re
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from gen_i18n import parse_yaml_file  # noqa: E402

# Must match enum class ugly::Quip (UglyQuip.h).
EVENTS = ['OpenPage', 'OpenGroup', 'SetValue', 'Pin', 'Unpin', 'Delete', 'ShellCross', 'ShellUgly', 'Sleep', 'Wake',
          'OpenBook', 'LeaveBook', 'OpenScreen']
# Conditions carrying data, by event (0 is the plain rotation). Must match the QuipWhen values in UglyQuip.h.
WHEN = {
    'OpenBook': {'GAP': 1, 'DONE': 2, 'END': 3, 'NEW': 4, 'LOW': 5, 'MID': 6, 'COUNT': 7},
    'LeaveBook': {'BLINK': 1, 'SHORT': 2, 'MID': 3, 'LONG': 4},
    'Sleep': {'ZERO': 1, 'LOW': 2},
    'Wake': {'GAP': 1},
}
# Conditions about books, the shell, sleep and waking, by their words in the table.
DATA = [
    (r'^Mở 1 cuốn sách', 'OpenBook', ''), (r'^Mở sách, tiến độ 0%$', 'OpenBook', 'NEW'),
    (r'^Mở sách, tiến độ 1 tới 10%', 'OpenBook', 'LOW'), (r'^Mở sách, tiến độ 11 tới 89%', 'OpenBook', 'MID'),
    (r'^Mở sách, tiến độ 90 tới 99%', 'OpenBook', 'END'), (r'^Mở sách đã đọc xong', 'OpenBook', 'DONE'),
    (r'^Mở sách bỏ từ 7 ngày', 'OpenBook', 'GAP'), (r'bội của 10', 'OpenBook', 'COUNT'),
    (r'^Thoát sách \(', 'LeaveBook', ''), (r'^Thoát sách, lượt đọc dưới 1 phút', 'LeaveBook', 'BLINK'),
    (r'^Thoát sách, lượt đọc 1 tới 4 phút', 'LeaveBook', 'SHORT'), (r'^Thoát sách, lượt đọc 5 tới 29', 'LeaveBook', 'MID'),
    (r'^Thoát sách, lượt đọc từ 30', 'LeaveBook', 'LONG'),
    (r'^Ghim 1 cuốn', 'Pin', ''), (r'^Gỡ ghim', 'Unpin', ''), (r'^Xoá 1 cuốn', 'Delete', ''),
    (r'^Chọn tenor/xấu', 'ShellUgly', ''), (r'^Chọn tenor/cross', 'ShellCross', ''),
    (r'^Lúc máy chuẩn bị ngủ', 'Sleep', ''), (r'^Chuẩn bị ngủ, hôm nay chưa đọc', 'Sleep', 'ZERO'),
    (r'^Chuẩn bị ngủ, hôm nay đọc 1 tới 14', 'Sleep', 'LOW'),
    (r'^Thức dậy \(', 'Wake', ''), (r'^Thức dậy sau từ 3 ngày', 'Wake', 'GAP'),
]
# The events the firmware says today: each has a quip() call in src (test/ugly_shell/check_assets.py holds the two
# lists equal). The lines of the others stay in the table, out of the blocks, until a screen says them: X3 flash.
SAID = {'OpenPage', 'OpenGroup', 'SetValue', 'Pin', 'Unpin', 'Delete', 'ShellCross'}
FIRST = ['OpenPage', 'Sleep', 'Wake', 'OpenBook', 'LeaveBook', 'Pin', 'Unpin', 'Delete', 'ShellCross', 'ShellUgly',
         'OpenGroup', 'OpenScreen', 'SetValue']
PAGES = {'Gần đây': 0, 'File': 1, 'Thống kê': 2, 'Cài đặt': 3, 'Yêu thích': 4}  # homerows::Page
LINES_PER_MARK = 32


def key16(*words):
    """FNV-1a over the words joined by '=', folded to 16 bits. Same as ugly::quipKey()."""
    h = 2166136261
    for b in '='.join(words).encode():
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return (h ^ (h >> 16)) & 0xFFFF


def bare(text):
    return re.sub(r'\s*\([^)]*\)\s*$', '', text).strip()


def slot_of(row, labels):
    """(event, key, when) for a row, or None when the firmware cannot tell its condition apart."""
    c, k = row['condition'], row['key']
    m = re.match(r'^Mở trang (.+)$', c)
    if m and m.group(1) in PAGES:
        return 'OpenPage', PAGES[m.group(1)], 0
    m = re.match(r'^Mở nhóm (.+)$', c)
    if m and bare(m.group(1)) in labels:
        return 'OpenGroup', key16(bare(m.group(1))), 0
    m = re.match(r'^Mở (.+)$', c)
    if m and bare(m.group(1)) in labels and 'sách' not in c:
        return 'OpenScreen', key16(bare(m.group(1))), 0
    m = re.match(r'^(.+?) = (.+)$', c)
    if m and bare(m.group(1)) in labels and bare(m.group(2)) in labels:
        return 'SetValue', key16(bare(m.group(1)), bare(m.group(2))), 0
    for pattern, event, when in DATA:
        if re.search(pattern, c):
            return event, 0, WHEN.get(event, {}).get(when, 0)
    return None


def build():
    rows = list(csv.DictReader(open(ROOT / 'scripts/ugly/quips.csv', encoding='utf-8')))
    vi_yaml = parse_yaml_file(ROOT / 'lib/I18n/translations/vietnamese.yaml')
    labels = {v.strip() for k, v in vi_yaml.items() if k.startswith('STR_')}
    # "như trên": the condition of the row above.
    last = None
    for r in rows:
        if r['condition'].startswith('như trên'):
            r['condition'] = last
        last = r['condition']
    # Lines of one slot sit next to each other in the blocks.
    order, slots, loose = [], {}, []
    for r in rows:
        s = slot_of(r, labels)
        if s is None or s[0] not in SAID:
            loose.append(r)
            continue
        slots.setdefault(s, []).append(r)
    # The lines said most often first: a lookup inflates the block only up to the line it needs.
    ranked = sorted(slots.items(), key=lambda kv: FIRST.index(kv[0][0]))
    for s, rs in ranked:
        order += rs
    # Slots in the order of their lines: a slot's first line is the sum of the counts before it, so the table
    # carries no offsets (the lookup scans it whole anyway).
    table = []
    for s, rs in ranked:
        assert len(rs) < 256, s
        table.append((EVENTS.index(s[0]), s[1], s[2], len(rs)))
    keys = [(e, k, w) for e, k, w, _ in table]
    assert len(keys) == len(set(keys)), 'two slots share a key: change a word or the hash'
    blocks = {}
    for lang in ('vi', 'en'):
        lines = [(r[lang] + '\0').encode() for r in order]
        raw = b''.join(lines)
        marks, at = [], 0
        for i, line in enumerate(lines):
            if i % LINES_PER_MARK == 0:
                marks.append(at)
            at += len(line)
        marks.append(at)
        assert at < 65536
        blocks[lang] = (zlib.compress(raw, 9), raw, marks)
    return order, table, blocks, loose


def emit(out_dir=None):
    order, table, blocks, loose = build()
    out = ['// Generated by scripts/ugly/gen_quips.py from scripts/ugly/quips.csv. Do not edit by hand.', '#pragma once', '',
           '#include <cstdint>', '', 'namespace ugly::quips {', '',
           'inline constexpr int LINES = %d, LINES_PER_MARK = %d;' % (len(order), LINES_PER_MARK), '']
    for lang in ('vi', 'en'):
        packed, raw, marks = blocks[lang]
        name = lang.upper()
        out.append('// %d lines, %d bytes raw, %d packed.' % (len(order), len(raw), len(packed)))
        out.append('inline constexpr uint8_t %s[] = {' % name)
        for i in range(0, len(packed), 16):
            out.append('    ' + ', '.join('0x%02x' % b for b in packed[i:i + 16]) + ',')
        out.append('};')
        out.append('// Where line 0, %d, %d... begins in the raw block, then its size.' % (LINES_PER_MARK, 2 * LINES_PER_MARK))
        out.append('inline constexpr uint16_t %s_AT[] = {%s};' % (name, ', '.join(map(str, marks))))
        out.append('')
    out.append('// An event, its key and condition, and how many lines it says, in the order of the lines in the blocks.')
    out.append('// event in the high nibble of `what`, the condition in the low one (ugly::Quip, ugly::when).')
    out.append('struct Slot {')
    out.append('  uint16_t key;')
    out.append('  uint8_t what, count;')
    out.append('};')
    out.append('inline constexpr Slot SLOTS[] = {')
    for e, k, w, n in table:
        assert e < 16 and w < 16
        out.append('    {%d, 0x%02x, %d},' % (k, e << 4 | w, n))
    out.append('};')
    out += ['', '}  // namespace ugly::quips', '']
    (Path(out_dir) if out_dir else ROOT / 'src/shells/ugly').joinpath('UglyQuips.h').write_text('\n'.join(out))
    vi, en = blocks['vi'][0], blocks['en'][0]
    print('quips: %d lines, %d slots, VI %d B, EN %d B packed, %d lines without a slot'
          % (len(order), len(table), len(vi), len(en), len(loose)))
    return loose


if __name__ == '__main__':
    loose = emit(sys.argv[1] if len(sys.argv) > 1 else None)
    if '-v' in sys.argv:
        for r in loose:
            print('  no slot:', r['key'], '|', r['condition'])
