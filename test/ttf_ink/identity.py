import os
from pathlib import Path
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]


class TtfIdentity(unittest.TestCase):
    def test_font_identity_ignores_all_six_ink_levels(self):
        text = (REPO / "src/SdCardFontSystem.cpp").read_text()
        start = text.index("int computeTtfFontId(")
        stop = text.index("\n}", start) + 2
        function = text[start:stop]
        program = "\n".join([
            "#include <cstdint>", "#include <type_traits>", "#include <cassert>", function,
            "template<typename Function> int identity(Function fn,uint8_t level) {",
            "if constexpr(std::is_invocable_r_v<int,Function,const char*,uint8_t,uint8_t>) return fn(\"Geist\",16,level);",
            "else return fn(\"Geist\",16); }",
            "int main() { for(uint8_t level=0;level<6;++level) assert(identity(&computeTtfFontId,level)==identity(&computeTtfFontId,0)); }",
        ])
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "identity.cpp"
            binary = Path(directory) / "identity"
            source.write_text(program)
            subprocess.run([os.environ.get("CXX", "c++"), "-std=c++20", str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
