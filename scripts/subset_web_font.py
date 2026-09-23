#!/usr/bin/env python3
"""Build src/network/html/assets/Geist.woff2, the face the on-device web pages embed.

The pages are served from flash, so the embedded font is cut to what they can show:
Latin with its extensions, Vietnamese, Cyrillic (book file names), punctuation, arrows and
box symbols; and the weight axis is narrowed to 400-700, the weights the pages and the
browser defaults for <strong> and <th> ask for. Run it again whenever the source face or
the pages change what they need.
"""
import subprocess
import sys
import tempfile
from pathlib import Path

from fontTools.ttLib import TTFont
from fontTools.varLib import instancer

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "lib/EpdFont/builtinFonts/source/Geist/Geist-Variable.ttf"
TARGET = ROOT / "src/network/html/assets/Geist.woff2"
UNICODES = ("U+0000-00FF,U+0100-024F,U+0300-036F,U+0400-052F,U+1EA0-1EFF,U+2000-206F,"
            "U+20A0-20CF,U+2190-21FF,U+2200-22FF,U+2500-27BF")
WEIGHTS = (400, 700)


def main() -> None:
    with tempfile.TemporaryDirectory() as work:
        cut = Path(work) / "cut.ttf"
        subprocess.run([sys.executable, "-m", "fontTools.subset", str(SOURCE), f"--unicodes={UNICODES}",
                        "--layout-features=*", f"--output-file={cut}"], check=True)
        face = instancer.instantiateVariableFont(TTFont(cut), {"wght": WEIGHTS})
        face.flavor = "woff2"
        face.save(TARGET)
    print(f"{TARGET.relative_to(ROOT)}: {TARGET.stat().st_size} bytes")


if __name__ == "__main__":
    main()
