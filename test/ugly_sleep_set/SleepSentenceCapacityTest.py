#!/usr/bin/env python3
import importlib.util
from pathlib import Path


repo = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("sleep_set_generator", repo / "scripts/ugly/gen_sleep_set.py")
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)


def accepted(text):
    generator.LINES = [("a", text, "x")]
    try:
        generator.text_blob(0)
    except AssertionError:
        return False
    return True


assert accepted("ấ" * 41 + "abcd"), "127 UTF-8 bytes must fit with the terminator"
assert not accepted("ấ" * 42 + "ab"), "128 UTF-8 bytes must be rejected before firmware truncation"
print("PASS: sleep sentence capacity stops at 127 UTF-8 bytes")
