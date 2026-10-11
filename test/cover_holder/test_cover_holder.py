#!/usr/bin/env python3
"""The cover placeholder: test_cover_holder.py <CoverHolderTool>

1. The master inflated from the strips in the firmware equals the PNG it was packed from.
2. The tenor/ink BMP at every frame size Home draws (X4 Pro 9 sizes, X3 2 sizes, 4 levels and 1 bit)
   equals reference.py, pixel for pixel (written from the definition of tenor/ink).
3. The X3 frames: heap peak, nothing left allocated, a clean false when any allocation is refused.
"""
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

import reference as ref

TOOL = sys.argv[1]


def run(*args):
    r = subprocess.run([TOOL, *map(str, args)], capture_output=True, text=True)
    return r


def main():
    failures = []
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        pgm = tmp / 'master.pgm'
        m = ref.master()
        pgm.write_bytes(b'P5\n%d %d\n255\n' % (m.shape[1], m.shape[0]) + m.tobytes())
        r = run('decode', pgm)
        print('decode', 'ok' if r.returncode == 0 else 'DIFFERENT')
        if r.returncode:
            failures.append('decode')
        for w, h in ref.X4_SIZES + ref.X3_SIZES:
            for four in (4, 1):
                out = tmp / f'ink_{w}x{h}_{four}.bmp'
                r = run('ink', w, h, four, out)
                if r.returncode:
                    failures.append(f'ink {w}x{h} {four}: tool failed')
                    continue
                got = ref.read_bmp(out)
                want = ref.expected(w, h, four == 4)
                diff = int((got != want).sum()) if got.shape == want.shape else -1
                print(f'ink {w}x{h} levels={four}: {diff} pixels differ')
                if diff:
                    failures.append(f'ink {w}x{h} {four}: {diff} pixels differ')
        r = run('check', pgm)
        print(r.stdout.strip())
        if r.returncode:
            failures.append('check')
    if failures:
        print('RED:', '; '.join(failures))
        return 1
    print('GREEN')
    return 0


if __name__ == '__main__':
    sys.exit(main())
