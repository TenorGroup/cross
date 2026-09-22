"""Built-in fonts must share exact metadata without changing offline consumers."""

from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import font_header_tools


def header(name):
    return f'''#pragma once
#include "EpdFontData.h"
static const uint8_t {name}Bitmaps[1] = {{
    0x80,
}};
static const EpdGlyph {name}Glyphs[] = {{
    {{1, 1, 16, 0, 1, 1, 0}},
}};
static const EpdUnicodeInterval {name}Intervals[] = {{
    {{0x41, 0x41, 0}},
}};
static const uint16_t {name}KernLeftCodepoints[] = {{
    65,
}};
static const uint8_t {name}KernLeftClassIds[] = {{
    1,
}};
static const int8_t {name}KernSparseValues[] = {{
    1,
}};
static const EpdFontData {name} = {{
    {name}Bitmaps, {name}Glyphs, {name}Intervals, 1, 10, 8, -2,
}};
'''


class FontMetadataTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="cross-font-metadata-")
        self.addCleanup(self.temp.cleanup)
        self.fonts = Path(self.temp.name)
        self.original = {}
        for name in ("tiny_a", "tiny_b"):
            path = self.fonts / f"{name}.h"
            path.write_text(header(name))
            self.original[name] = path.read_text()

    def pool(self, *args, success=True):
        result = subprocess.run(
            [sys.executable, str(ROOT / "scripts/deduplicate_font_metadata.py"), str(self.fonts), *args],
            capture_output=True, text=True,
        )
        self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
        return result

    def files(self):
        return {p.name: p.read_bytes() for p in self.fonts.glob("*.h")}

    def test_pool_preserves_glyphs_metrics_kerning_and_types(self):
        before = {n: font_header_tools.read_header(self.fonts / f"{n}.h") for n in self.original}
        self.pool()
        for name in self.original:
            self.assertEqual(before[name], font_header_tools.read_header(self.fonts / f"{name}.h"))
        text = (self.fonts / "builtin_font_metadata.h").read_text()
        self.assertEqual(text.count("inline constexpr EpdUnicodeInterval"), 1)
        self.assertEqual(text.count("inline constexpr uint16_t"), 1)
        self.assertEqual(text.count("inline constexpr uint8_t"), 1)
        self.assertEqual(text.count("inline constexpr int8_t"), 1)

    def test_repeated_pooling_and_partial_regeneration_are_identical(self):
        self.pool()
        before = self.files()
        self.pool("--check")
        self.pool()
        self.assertEqual(before, self.files())
        (self.fonts / "tiny_a.h").write_text(self.original["tiny_a"])
        self.pool("--check", success=False)
        self.pool()
        self.assertEqual(before, self.files())

    def test_expanded_header_is_self_contained_for_offline_rewrites(self):
        self.pool()
        source = self.fonts / "tiny_a.h"
        destination = self.fonts / "output" / "tiny_a.h"
        destination.parent.mkdir()
        destination.write_text(font_header_tools.expand_header(source))
        self.assertEqual(font_header_tools.read_header(source), font_header_tools.read_header(destination))
        self.assertNotIn('"builtin_font_metadata.h"', destination.read_text())

    def test_missing_shared_target_fails_before_rewriting(self):
        self.pool()
        pool = self.fonts / "builtin_font_metadata.h"
        pool.write_text("#pragma once\n")
        before = self.files()
        self.pool(success=False)
        self.assertEqual(before, self.files())

    def test_shipped_headers_are_already_pooled(self):
        result = subprocess.run(
            [sys.executable, str(ROOT / "scripts/deduplicate_font_metadata.py"),
             str(ROOT / "lib/EpdFont/builtinFonts"), "--check"], capture_output=True, text=True,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_shared_tables_have_one_address_across_translation_units(self):
        compiler = shutil.which("c++")
        if not compiler:
            self.fail("A C++ compiler is required to verify shared table linkage")
        self.pool()
        (self.fonts / "a.cpp").write_text(
            '#include "tiny_a.h"\nextern "C" const void* address_a() { return tiny_a.intervals; }\n')
        (self.fonts / "b.cpp").write_text(
            '#include "tiny_b.h"\nextern "C" const void* address_b() { return tiny_b.intervals; }\n')
        (self.fonts / "main.cpp").write_text('''extern "C" const void* address_a();
extern "C" const void* address_b();
int main() { return address_a() != address_b(); }
''')
        binary = self.fonts / "linkage"
        subprocess.run([compiler, "-std=c++17", "-Os", "-I", str(ROOT / "lib/EpdFont"),
                        str(self.fonts / "a.cpp"), str(self.fonts / "b.cpp"),
                        str(self.fonts / "main.cpp"), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main(verbosity=2)
