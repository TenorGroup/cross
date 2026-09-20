"""Structural coverage ledger for custom list renderers; not framebuffer proof."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


class ListRenderWiring(unittest.TestCase):
    def test_every_custom_list_renderer_consumes_measured_feedback(self):
        candidates = {"UiListActivity": ROOT / "src/activities/UiListActivity.cpp",
                      "OpdsBookBrowserActivity": ROOT / "src/activities/browser/OpdsBookBrowserActivity.cpp"}
        for header in (ROOT / "src/activities").rglob("*.h"):
            for name in re.findall(r"class\s+(\w+)[^{;]*:\s*public\s+Ui(?:Tab)?ListActivity", header.read_text()):
                candidates[name] = header.with_suffix(".cpp")
        custom = []
        inherited = []
        for name, source in sorted(candidates.items()):
            if not source.exists():
                continue
            content = source.read_text()
            marker = f"void {name}::render(RenderLock&&)"
            if marker not in content:
                inherited.append(name)
                continue
            body = content.split(marker, 1)[1].split("\n}", 1)[0]
            self.assertIn("renderSettledList(", body, f"{name} bypasses measured viewport feedback")
            custom.append(name)
        self.assertGreaterEqual(len(custom), 10)
        print("STRUCTURAL custom renderers:", ", ".join(custom))
        print("STRUCTURAL inherited renderers:", ", ".join(inherited))


if __name__ == "__main__":
    unittest.main()
