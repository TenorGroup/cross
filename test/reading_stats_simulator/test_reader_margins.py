"""Reader content margins must use the requested inset on both sides."""

from pathlib import Path
import re
import unittest


REPO = Path(__file__).resolve().parents[2]
READER = REPO / "src/activities/reader/ReaderActivity.cpp"


def reading_margins_body() -> str:
    source = READER.read_text(encoding="utf-8")
    start = source.index("void ReaderActivity::readingMargins(")
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


class ReaderMarginsTest(unittest.TestCase):
    def test_tenor_reader_uses_equal_requested_side_margins(self):
        body = reading_margins_body()
        tenor_branch = body[body.index("if (tenorchrome::enabled())") :]
        left = re.search(r"\bleft\s*=\s*([^;]+);", tenor_branch)
        right = re.search(r"\bright\s*=\s*([^;]+);", tenor_branch)
        self.assertIsNotNone(left, body)
        self.assertIsNotNone(right, body)
        self.assertEqual(left.group(1).strip(), "margin")
        self.assertEqual(right.group(1).strip(), "margin")


if __name__ == "__main__":
    unittest.main()
