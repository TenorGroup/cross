#!/usr/bin/env python3
"""The wolfSSL build flags in platformio.ini do not make the vendored library warn.

Two warnings filled every C3 build log (132 per build):
  user_settings.h: "NO_WOLFSSL_ESP32_CRYPT_RSA_PRI" redefined  (the -D flag is 1, the header's #define is empty)
  settings.h: #warning wolfssl/options.h included in compiled wolfssl library object
              (-DWOLFSSL_OPTIONS_H only stood in for "a custom configuration"; WOLFSSL_CUSTOM_CONFIG is that)
Both are decided by the preprocessor, so this runs it with the flags of [base] and the real header text.
"""
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WOLF = ROOT / "third_party/wolfssl/src"


def base_defines():
    """The -D tokens of [base] build_flags that the preprocessor can take as they are."""
    text = (ROOT / "platformio.ini").read_text()
    section = text[text.index("[base]"):]
    section = section[:section.index("\n[", 1)]
    flags = section[section.index("build_flags"):]
    out = []
    for line in flags.splitlines():
        line = line.strip()
        if line.startswith(("#", ";")):
            continue
        for token in re.findall(r"(?<!\S)-D([A-Za-z_]\w*(?:=[^\s\"$]*)?)(?=\s|$)", line.split(" ;")[0]):
            out.append(token)
    return out


def preprocess(source, defines):
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "t.c"
        path.write_text(source)
        cmd = ["cc", "-fsyntax-only", "-Werror", "-Wall", str(path)] + [f"-D{d}" for d in defines]
        return subprocess.run(cmd, capture_output=True, text=True)


class WolfsslFlags(unittest.TestCase):
    def test_hardware_rsa_flag_matches_the_header_define(self):
        defines = base_defines()
        self.assertTrue(any(d.startswith("NO_WOLFSSL_ESP32_CRYPT_RSA_PRI") for d in defines))
        header = (WOLF / "user_settings.h").read_text()
        lines = re.findall(r"(?m)^\s*#define NO_WOLFSSL_ESP32_CRYPT_RSA_PRI\s*$", header)
        self.assertTrue(lines, "user_settings.h no longer defines it; this check has nothing to compare")
        result = preprocess("\n".join(lines) + "\n", defines)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_library_objects_do_not_see_the_options_h_warning(self):
        settings = (WOLF / "wolfssl/wolfcrypt/settings.h").read_text()
        cond = re.search(r"(?m)^#if (!defined\(WOLFSSL_CUSTOM_CONFIG\) && \\\n(?:.*\\\n)*.*)\n\s+#if !defined\(_MSC_VER\)", settings)
        self.assertTrue(cond, "settings.h changed: update the pattern")
        source = "#if " + cond.group(1).replace("\\\n", " ") + "\n#error options.h warning\n#endif\n"
        result = preprocess(source, base_defines() + ["BUILDING_WOLFSSL"])
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
