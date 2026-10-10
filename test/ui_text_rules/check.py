#!/usr/bin/env python3
"""Rules for the Vietnamese and English UI strings: one ellipsis character, no exclamation marks,
no dashes or middle dots, no guillemet on STR_BACK, matching placeholders, and error lines that
say what happened instead of "Loi ..." or "... that bai". Run: python3 test/ui_text_rules/check.py"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from gen_i18n import parse_yaml_file

TR = ROOT / 'lib/I18n/translations'
FORMAT = re.compile(r'%(?:\d+\$)?[-+#0]*(?:\d+|\*)?(?:\.(?:\d+|\*))?(?:hh|ll|[hljztL])?[diuoxXfFeEgGaAcsp%]|\{[^{}]*\}')
FACES = [ROOT / 'lib/EpdFont/builtinFonts' / f'geist_{s}_{w}.h' for s in (10, 12, 14, 16) for w in ('regular', 'bold')] + \
        [ROOT / 'lib/EpdFont/builtinFonts/geist_8_regular.h']
# STR_ERROR_MSG is the bare label above an error line ("Lỗi:" / "Error:"), not an error sentence.
LABELS = {'STR_ERROR_MSG'}


def strings(name):
    data = parse_yaml_file(TR / name)
    return {k: v for k, v in data.items() if k.startswith('STR_')}


def main():
    vi, en = strings('vietnamese.yaml'), strings('english.yaml')
    problems = []
    for lang, data in (('vi', vi), ('en', en)):
        for key, text in data.items():
            if '...' in text:
                problems.append(f'{lang} {key}: three dots, use U+2026: {text!r}')
            if '!' in text:
                problems.append(f'{lang} {key}: exclamation mark: {text!r}')
            if re.search('[–—·]', text):
                problems.append(f'{lang} {key}: dash or middle dot: {text!r}')
    for lang, data in (('vi', vi), ('en', en)):
        if '«' in data['STR_BACK']:
            problems.append(f'{lang} STR_BACK: guillemet: {data["STR_BACK"]!r}')
    for key in sorted(set(vi) & set(en)):
        if FORMAT.findall(vi[key]) != FORMAT.findall(en[key]):
            problems.append(f'{key}: placeholders differ: {FORMAT.findall(vi[key])} vs {FORMAT.findall(en[key])}')
    for key, text in vi.items():
        if key in LABELS:
            continue
        if re.match(r'\s*Lỗi\b', text):
            problems.append(f'vi {key}: starts with "Lỗi": {text!r}')
        if 'thất bại' in text.lower():
            problems.append(f'vi {key}: "thất bại": {text!r}')
    for key, text in en.items():
        if key not in LABELS and re.match(r'\s*error\s*:', text, re.I):
            problems.append(f'en {key}: starts with "Error:": {text!r}')
    uses_ellipsis = any('…' in t for t in list(vi.values()) + list(en.values()))
    for face in FACES:
        if uses_ellipsis and '// U+2026' not in face.read_text(encoding='utf-8'):
            problems.append(f'{face.name}: no glyph for U+2026')
    if problems:
        print('\n'.join(problems))
        print(f'{len(problems)} problems')
        return 1
    print(f'ok: {len(vi)} vi and {len(en)} en strings')
    return 0


if __name__ == '__main__':
    sys.exit(main())
