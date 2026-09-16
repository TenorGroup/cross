"""Validate Chinese translation and generated UI glyphs without fontTools or network."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys
from font_header_tools import read_header

NAMES = ('bevietnampro_8_regular', 'bevietnampro_10_regular', 'bevietnampro_10_bold', 'geist_12_regular', 'geist_12_bold')
FORMAT = re.compile(r'%(?:\d+\$)?[-+#0 ]*(?:\d+|\*)?(?:\.(?:\d+|\*))?(?:hh|ll|[hljztL])?[diuoxXfFeEgGaAcsp%]')

def required(data):
    return {ord(c) for k, v in data.items() if k.startswith('STR_') or k == '_language_name' for c in v if c not in '\n\r\t' and not 0xe000 <= ord(c) <= 0xf8ff}

def fingerprint(cps):
    return hashlib.sha256(','.join(map(str, sorted(cps))).encode()).hexdigest()

def validate(root, chinese=None, headers=None, baseline=None):
    sys.path.insert(0, str(root/'scripts'))
    from gen_i18n import parse_yaml_file
    en = parse_yaml_file(root/'lib/I18n/translations/english.yaml')
    zh = parse_yaml_file(chinese or root/'lib/I18n/translations/chinese.yaml')
    enkeys = {k for k in en if k.startswith('STR_')}
    zhkeys = {k for k in zh if k.startswith('STR_')}
    assert enkeys == zhkeys, ('key mismatch', enkeys ^ zhkeys)
    assert zh['_language_code'] == 'ZH_HANS' and zh['_bcp47'] == 'zh-Hans'
    for path in (root/'lib/I18n/translations').glob('*.yaml'):
        if path.name == 'chinese.yaml':
            continue
        other = parse_yaml_file(path)
        for key in ('_language_code', '_bcp47', '_order'):
            assert zh[key] != other[key], (key, 'duplicate metadata', path.name)
    for k in enkeys:
        assert FORMAT.findall(en[k]) == FORMAT.findall(zh[k]), (k, 'printf mismatch')
        assert re.findall('[\ue000-\uf8ff]', en[k]) == re.findall('[\ue000-\uf8ff]', zh[k]), (k, 'button mismatch')
        assert en[k].count('\n') == zh[k].count('\n'), (k, 'newline mismatch')
        assert not any(c in zh[k] for c in '\u2013\u2014'), (k, 'forbidden punctuation')
    cps = required(zh)
    headers = headers or root/'lib/EpdFont/builtinFonts'
    base = json.loads(baseline.read_text()) if baseline else None
    results = []
    for name in NAMES:
        h = read_header(headers/(name+'.h'))
        missing = cps - h['glyphs'].keys()
        assert not missing, (name, 'missing glyphs', len(missing), sorted(missing)[:8])
        assert f'Chinese UI charset SHA256: {fingerprint(cps)}' in (headers/(name+'.h')).read_text(), (name, 'stale charset fingerprint')
        if base:
            assert h['metrics'] == base[name]['metrics'], (name, 'metrics changed')
            assert h['kern'] == base[name]['kern'], (name, 'kerning changed')
            for cp, digest in base[name]['hashes'].items():
                assert h['hashes'].get(cp) == digest, (name, 'old glyph changed', cp)
        top = max(h['glyphs'][cp][0][4] for cp in cps if cp >= 0x2e80)
        bottom = min(h['glyphs'][cp][0][4] - h['glyphs'][cp][0][1] for cp in cps if cp >= 0x2e80)
        assert top <= h['metrics'][1] and bottom >= h['metrics'][2], (name, 'CJK exceeds line metrics', top, bottom, h['metrics'])
        results.append({'font': name, 'required': len(cps), 'glyphs': len(h['glyphs']), 'bitmap_bytes': h['bitmap_bytes'], 'cjk_top_bottom': [top,bottom], 'line_metrics': h['metrics']})
    return {'keys': len(enkeys), 'charset_sha256': fingerprint(cps), 'fonts': results}

if __name__ == '__main__':
    p=argparse.ArgumentParser();p.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[1]);p.add_argument('--chinese',type=Path);p.add_argument('--headers',type=Path);p.add_argument('--baseline',type=Path);a=p.parse_args()
    print(json.dumps(validate(a.root,a.chinese,a.headers,a.baseline),ensure_ascii=False,indent=2))
