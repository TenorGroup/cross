"""Quantize the approved grayscale input with Bayer V3 or optional V4."""
import importlib.util
import os
from pathlib import Path

import numpy as np


def quantize(gray, variant='V3', **inputs):
    if variant == 'V3':
        bayer = np.array([[0, 8, 2, 10], [12, 4, 14, 6],
                          [3, 11, 1, 9], [15, 7, 13, 5]], dtype=np.int16)
        y, x = np.indices(gray.shape)
        adjusted = np.clip(gray.astype(np.int16) + (bayer[y % 4, x % 4] - 8) * 5, 0, 255)
        return (np.digitize(adjusted, [64, 128, 192]) * 85).astype(np.uint8)
    if variant != 'V4':
        raise ValueError('Choose V3 or V4')
    root = os.environ.get('TENOR_DITHER_ROOT')
    if not root:
        raise SystemExit('V4 requires TENOR_DITHER_ROOT pointing to tenordither57; V3 is self-contained')
    path = Path(root) / 'scripts/v3/khoi_dong.py'
    if not path.is_file():
        raise SystemExit('V4 quantizer is missing: ' + str(path))
    import sys
    sys.path.insert(0, str(path.parent))
    spec = importlib.util.spec_from_file_location('boot_quantizer', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    panel, _ = module.boot_panel(gray, **inputs)
    return panel
