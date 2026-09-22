"""Decode shipped compressed glyphs and compare every pixel to 1-bit references."""
import argparse
import json
from pathlib import Path
import re
import sys
import zlib
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'scripts'))
from font_header_tools import expand_header, read_header
from check_chinese_ui import required
from gen_i18n import parse_yaml_file


def inspect(header, reference):
    text = expand_header(header)
    def body(suffix):
        return re.search(r'\w+'+suffix+r'\[.*?\]\s*=\s*\{(.*?)\n\};', text, re.S).group(1)
    bitmap = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]+)', body('Bitmaps')))
    glyphs = [list(map(int, re.findall(r'-?\d+', line))) for line in re.findall(r'\{([^{}]+)\}', body('Glyphs'))]
    groups = [list(map(int, re.findall(r'\d+', line))) for line in re.findall(r'\{([^{}]+)\}', body('Groups'))]
    intervals = [[int(x.strip(), 0) for x in line.split(',')] for line in re.findall(r'\{([^{}]+)\}', body('Intervals'))]
    ref = read_header(reference)
    decoded = {}
    for offset, length, unpacked_length, count, first in groups:
        assert unpacked_length <= 2048, ('unbounded group', unpacked_length)
        unpacked = zlib.decompress(bitmap[offset:offset+length], -15)
        assert len(unpacked) == unpacked_length
        pos = 0
        for index in range(first, first+count):
            width, height, advance, left, top, data_length, data_offset = glyphs[index]
            stride = (width+3)//4
            pixels = []
            for y in range(height):
                for x in range(width):
                    value = (unpacked[pos+y*stride+x//4] >> (6-2*(x%4))) & 3
                    assert value in (0, 3), ('gray UI pixel', index, value)
                    pixels.append(value == 3)
            pos += stride*height
            decoded[index] = pixels
        assert pos == len(unpacked)
    for lo, hi, first in intervals:
        for cp in range(lo, hi+1):
            index = first+cp-lo
            metrics, packed = ref['glyphs'][cp]
            assert tuple(glyphs[index][:5]) == tuple(metrics), ('metrics', cp)
            expected = [bool(packed[i//8] & (128 >> (i%8))) for i in range(metrics[0]*metrics[1])]
            assert decoded[index] == expected, ('pixel support changed', cp)
    for locale in ('vietnamese', 'english', 'chinese'):
        wanted = required(parse_yaml_file(ROOT/f'lib/I18n/translations/{locale}.yaml')) | set(map(ord, '界面字号小中大'))
        assert wanted <= ref['glyphs'].keys(), (locale, sorted(wanted-ref['glyphs'].keys()))
    return {'glyphs': len(glyphs), 'compressed_bitmap_bytes': len(bitmap), 'reference_bitmap_bytes': ref['bitmap_bytes'], 'groups':len(groups), 'max_group_bytes':max(g[2] for g in groups), 'metrics':ref['metrics']}


def main():
    parser = argparse.ArgumentParser(); parser.add_argument('--generated', type=Path, required=True); args = parser.parse_args()
    results = {}
    for size in (14,16):
        for style in ('regular','bold'):
            name = f'geist_{size}_{style}'
            results[name] = inspect(args.generated/(name+'.h'), args.generated/(name+'-reference.h'))
    print(json.dumps(results, indent=2))

if __name__ == '__main__':
    main()
