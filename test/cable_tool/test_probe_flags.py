"""An X4 Pro probe build must carry the UI acceptance commands (HOME_TAB, HOME_NEXT, ...).

main.cpp wraps those four CMD: lines in TENOR_UI_ACCEPTANCE and an unknown command falls through without a
word. The 25/10/2026 X4 Pro journey ran on x4pro-probe, which lacked the flag: CMD:HOME_TAB did nothing and
the two CMD:TAP after it landed in the open book.
"""
import configparser
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def load():
    ini = configparser.ConfigParser(interpolation=None, inline_comment_prefixes=(";",), strict=False)
    for name in ("platformio.ini", "platformio.measure.ini"):
        ini.read(ROOT / name)
    return ini


def flags(ini, section, seen=()):
    """The -D flags of a section: its own build_flags with ${x.build_flags} expanded, else its parents'."""
    if section in seen or section not in ini:
        return set()
    seen = seen + (section,)
    raw = ini[section].get("build_flags")
    if raw is None:
        out = set()
        for parent in re.split(r"[,\s]+", ini[section].get("extends", "").strip()):
            if parent:
                out |= flags(ini, parent if parent in ini else parent, seen)
        return out
    out = set()
    for ref in re.findall(r"\$\{([^.}]+)\.build_flags\}", raw):
        out |= flags(ini, ref, seen)
    out |= set(re.findall(r"(?m)^\s*-D(\w+)", re.sub(r"\$\{[^}]*\}", "", raw)))
    return out


def chain(ini, section, seen=()):
    if section in seen or section not in ini:
        return set()
    out = {section}
    for parent in re.split(r"[,\s]+", ini[section].get("extends", "").strip()):
        if parent:
            out |= chain(ini, parent, seen + (section,))
    return out


class ProbeFlags(unittest.TestCase):
    def test_the_x4_pro_probe_and_what_builds_on_it_have_the_ui_acceptance_commands(self):
        # x4pro-ble-probe is the Bluetooth journey profile and stays without them on purpose
        # (test/ble_integration/test_s3_profiles.py asserts it).
        ini = load()
        probes = [s for s in ini.sections() if s.startswith("env:") and "env:x4pro-probe" in chain(ini, s)]
        self.assertIn("env:x4pro-probe", probes)
        self.assertIn("env:m-x4pro", probes)
        for env in probes:
            with self.subTest(env=env):
                self.assertIn("TENOR_PRESS_PROBE", flags(ini, env))
                self.assertIn("TENOR_UI_ACCEPTANCE", flags(ini, env))
                # main.cpp's HEAP_INFO line (under TENOR_UI_ACCEPTANCE) calls bleturner::startStackLeft(), which
                # BlePageTurner.h declares only with BLE_PAGE_TURNER_STACK_PROBE: without it the build stops.
                self.assertIn("BLE_PAGE_TURNER_STACK_PROBE", flags(ini, env))


def guards(path, needle):
    """The #if / #ifdef lines open around the first line containing needle."""
    stack = []
    for line in path.read_text().splitlines():
        directive = line.strip()
        if directive.startswith(("#if", "#ifdef", "#ifndef")):
            stack.append(directive)
        elif directive.startswith("#endif"):
            stack.pop()
        elif directive.startswith(("#else", "#elif")):
            stack[-1] = directive
        elif needle in line:
            return stack
    return None


class WebDavProbe(unittest.TestCase):
    """CMD:WEBDAV_PULL opens the WebDAV pull screen, so the heap of the TLS session behind it can be measured
    without the key presses through Settings > Connections (X4 Classic and X4 Pro only; the X3 has no WebDAV)."""

    def test_command_exists_only_in_acceptance_builds_and_not_on_the_x3(self):
        stack = guards(ROOT / "src/main.cpp", 'cmd == "WEBDAV_PULL"')
        self.assertIsNotNone(stack, "main.cpp has no CMD:WEBDAV_PULL")
        self.assertTrue(any("TENOR_UI_ACCEPTANCE" in g for g in stack), stack)
        self.assertTrue(any("FREEINK_DEVICE_X3" in g for g in stack), stack)


if __name__ == "__main__":
    unittest.main(verbosity=2)
