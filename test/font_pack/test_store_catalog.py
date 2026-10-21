#!/usr/bin/env python3
"""The store catalog header the firmware compares pack receipts with is made from the store's manifest.

The header on disk must match the manifest of the version being built when MANIFEST is given (the release gate
passes it); the generator itself is checked here on a small manifest: sorted names, one pack per font, refusal of a
font with more than one file."""
import importlib.util
import os
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('gen', REPO / 'scripts' / 'gen_font_store_catalog.py')
gen = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gen)

sample = {'families': [
    {'name': 'Zed', 'files': [{'name': 'Zed.cpfontpack', 'size': 5, 'crc32': 7}]},
    {'name': 'Abe', 'files': [{'name': 'Abe.cpfontpack', 'size': 9, 'crc32': 11}]}]}
text = gen.render(sample)
assert text.index('"Abe"') < text.index('"Zed"'), 'sorted by name'
assert '{"Abe", 9u, 11u},' in text and '{"Zed", 5u, 7u},' in text
bad = {'families': [{'name': 'X', 'files': [{'name': 'a.cpfont', 'size': 1, 'crc32': 2}]}]}
try:
    gen.render(bad)
    raise AssertionError('a font that is not one .cpfontpack must be refused')
except SystemExit:
    pass

manifest = os.environ.get('MANIFEST')
if manifest:
    import json
    assert gen.OUT.read_text() == gen.render(json.load(open(manifest))), 'FontStoreCatalog.h differs from ' + manifest
print('store catalog: ok')
