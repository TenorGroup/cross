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
        delegating = []
        inherited = []
        for name, source in sorted(candidates.items()):
            if not source.exists():
                continue
            content = source.read_text()
            found = re.search(rf"void {name}::render\(RenderLock&&(?: \w+)?\)", content)
            if not found:
                inherited.append(name)
                continue
            body = content[found.end():].split("\n}", 1)[0]
            if "renderSettledList(" in body:
                custom.append(name)
            else:
                # Upstream's list screens (and the OPDS browser, its note pages aside) paint a popup
                # or a status page first and hand the rows to the base render, which settles.
                self.assertIn("UiListActivity::render(", body, f"{name} bypasses measured viewport feedback")
                delegating.append(name)
        # The clock screen is upstream's ClockSettingsActivity since 1.6.5 (#3562); it keeps
        # its popup in render() and settles through UiListActivity::render(), where the
        # Tenor clock screen it replaced had its own renderer. The OPDS browser is upstream's
        # CatalogActivity descendant since 1.1.0 and delegates the same way.
        self.assertIn("ClockSettingsActivity", delegating)
        self.assertIn("OpdsBookBrowserActivity", delegating)
        self.assertGreaterEqual(len(custom) + len(delegating), 9)
        print("STRUCTURAL custom renderers:", ", ".join(custom))
        print("STRUCTURAL delegating renderers:", ", ".join(delegating))
        print("STRUCTURAL inherited renderers:", ", ".join(inherited))


if __name__ == "__main__":
    unittest.main()
