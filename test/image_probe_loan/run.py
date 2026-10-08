#!/usr/bin/env python3
"""Run the real chapter image-probe region against constrained stream boundaries."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('source', type=Path)
parser.add_argument('output', type=Path)
args = parser.parse_args()
text = (args.source / 'lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp').read_text()
start = text.index('              ImageDimensions dims = {0, 0};')
end = text.index('              if (gotDimensions) {', start)
region = text[start:end]
fixture = Path(__file__).with_name('fixture.cpp').read_text()
args.output.mkdir(parents=True, exist_ok=True)
projected = args.output / 'probe.cpp'
projected.write_text(fixture.replace('@@PROBE@@', region))
binary = args.output / 'probe'
subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                '-Wno-unused-variable', '-fsanitize=address,undefined', '-g',
                str(projected), '-o', str(binary)], check=True)
raise SystemExit(subprocess.run([str(binary)]).returncode)
