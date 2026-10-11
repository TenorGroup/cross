import argparse
from pathlib import Path
import subprocess
p = argparse.ArgumentParser()
p.add_argument('--output', type=Path, required=True)
p.add_argument('--cxx', default='c++')
p.add_argument('--source', type=Path)
p.add_argument('--dither', action='store_true')
p.add_argument('--reference-only', action='store_true')
a = p.parse_args()
repo = Path(__file__).resolve().parents[2]
a.output.mkdir(parents=True, exist_ok=True)
s = (a.source or repo/'lib/GfxRenderer/GfxRenderer.cpp').read_text()
start = s.index('bool GfxRenderer::drawBitmap(const Bitmap&')
end = s.index('\nbool GfxRenderer::drawBitmap1Bit', start)
body = s[start:end]
(a.output/'paint.inc').write_text(body)
helper = repo/'lib/GfxRenderer/BitmapRow.h'
(a.output/'row.inc').write_text(helper.read_text() if helper.exists() else '')
exe = a.output/'bitmap-writer'
subprocess.run([a.cxx, '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror',
               '-I'+str(a.output), '-I'+str(repo/'lib/GfxRenderer'), '-I'+str(repo/'lib/Memory'),
               str(repo/'test/bitmap_writer/paint.cpp'), '-o', str(exe)], check=True)
raise SystemExit(subprocess.run([str(exe), 'dither' if a.dither else 'plain', 'reference' if a.reference_only else 'writer']).returncode)
