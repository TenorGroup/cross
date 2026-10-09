import argparse
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--repo', type=Path, required=True)
args = parser.parse_args()
repo = args.repo
checks = {
    'File/X4 bitmap route': ('freeink-sdk/libs/ui/FreeInkUI/include/FreeInkUIGfxRenderer.h', 'bitmapPainter_'),
    'inline controls': ('lib/GfxRenderer/InlineSymbols.cpp', 'shapePainter'),
    'theme installs bitmap route': ('src/components/UITheme.cpp', 'setBitmapPainter'),
    'status/root icons': ('src/components/TenorMenuChrome.cpp', 'ugly::iconAsset'),
    'stable page seed': ('src/activities/ActivityManager.cpp', 'uglyIconFrame.bind'),
    'shared pen': ('src/shells/ugly/UglyIconInk.cpp', 'penStroke'),
}
failures = 0
for label, (filename, token) in checks.items():
    present = (repo / filename).exists() and token in (repo / filename).read_text()
    failures += not present
    print(('GREEN' if present else 'RED'), label)
print(f'{len(checks) - failures}/{len(checks)} checks')
raise SystemExit(bool(failures))
