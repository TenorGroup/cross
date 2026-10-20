"""Noto Serif left the firmware in v1.0.69 (upstream #4022): it is a card font now, 896 KB back per image."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


class NoBuiltinSerif(unittest.TestCase):
    def test_no_serif_headers(self):
        self.assertEqual(sorted(p.name for p in (ROOT / "lib/EpdFont/builtinFonts").glob("notoserif_*.h")), [])

    def test_no_serif_ids_in_firmware(self):
        hits = [f"{p.relative_to(ROOT)}:{n}" for p in (ROOT / "src").rglob("*.[ch]*")
                for n, line in enumerate(p.read_text(errors="replace").splitlines(), 1)
                if re.search(r"NOTOSERIF_\d+_FONT_ID|notoserif_\d+_", line)]
        self.assertEqual(hits, [])


if __name__ == "__main__":
    unittest.main(verbosity=2)
