#!/usr/bin/env python3
"""scripts/gen_i18n.py writes the same I18nKeys.h, I18nStrings.h and I18nStrings.cpp for the same tree however it is run.

A PlatformIO build runs it with the unused keys stripped; a host test run by hand used to keep them, so two worktrees of
the same source held generated files that differed by the unused keys and the glyphs only they used. One mode now: the
default (and --strip-unused) strips, --keep-unused is the way to keep every key. The output must not depend on what the
output directory held before, nor on the hash seed.
"""
import filecmp
import os
from pathlib import Path
import subprocess
import sys
import tempfile

REPO = Path(__file__).resolve().parents[2]
SCRIPT = REPO / 'scripts/gen_i18n.py'
FILES = ('I18nKeys.h', 'I18nStrings.h', 'I18nStrings.cpp')


def generate(out, *flags, seed='0'):
    out.mkdir(parents=True, exist_ok=True)
    subprocess.run([sys.executable, '-I', str(SCRIPT), str(REPO / 'lib/I18n/translations'), str(out), *flags],
                   cwd=REPO, env={**os.environ, 'PYTHONHASHSEED': seed}, check=True, capture_output=True)


def same(a, b, what):
    for name in FILES:
        assert filecmp.cmp(a / name, b / name, shallow=False), f'{what}: {name} differs'


def main():
    with tempfile.TemporaryDirectory(prefix='i18n-gen-') as tmp:
        tmp = Path(tmp)
        generate(tmp / 'default')
        generate(tmp / 'explicit', '--strip-unused', seed='7')
        same(tmp / 'default', tmp / 'explicit', 'default and --strip-unused')
        # An output directory that held a run which kept every key.
        generate(tmp / 'again', '--keep-unused')
        kept = (tmp / 'again' / 'I18nKeys.h').read_text()
        generate(tmp / 'again')
        same(tmp / 'default', tmp / 'again', 'after a --keep-unused run')
        assert len((tmp / 'default' / 'I18nKeys.h').read_text()) <= len(kept), 'stripping grew the keys'
    print('GREEN: gen_i18n.py output does not depend on the way it is run')


if __name__ == '__main__':
    main()
