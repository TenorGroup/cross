from pathlib import Path
import unittest


REPO = Path(__file__).resolve().parents[2]
SOURCE = REPO / "src/activities/settings/TextSettingsActivity.cpp"


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    open_brace = source.index("{", start)
    depth = 0
    for index in range(open_brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[open_brace + 1:index]
    raise AssertionError(f"unterminated function: {signature}")


class InkQuickCycleContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SOURCE.read_text()
        cls.confirm = function_body(cls.source, "void TextSettingsActivity::confirmStyleRow")
        cls.confirm_label = function_body(cls.source, "const char* TextSettingsActivity::confirmLabelText")
        cls.value = function_body(cls.source, "std::string TextSettingsActivity::styleValueText")
        cls.render = function_body(cls.source, "void TextSettingsActivity::render")

    def test_short_press_cycles_requested_level_and_persists(self):
        ink_case = self.confirm.split("case StyleRow::InkWeight:", 1)[1].split(
            "case StyleRow::FocusReading:", 1
        )[0]
        self.assertIn("readerInk::next(SETTINGS.readerInkWeight)", ink_case)
        self.assertNotIn("nextAvailable", ink_case)
        self.assertNotIn("optionPopup_.show", ink_case)
        self.assertNotIn("finish()", ink_case)
        self.assertIn("SETTINGS.saveToFile();", self.confirm)
        self.assertIn("requestUpdate();", self.confirm)
        style_label = self.confirm_label.split("case Tab::Style:", 1)[1].split("default:", 1)[0]
        self.assertIn("return tr(STR_TOGGLE);", style_label)

    def test_row_keeps_requested_label_and_missing_variant_is_explicit(self):
        self.assertIn(
            "INK_WEIGHT_IDS[readerInk::clamp(SETTINGS.readerInkWeight)]", self.value
        )
        self.assertIn(
            "!readerInk::available(SETTINGS.readerInkWeight, sdFontSystem.availableWeightMask())",
            self.render,
        )


if __name__ == "__main__":
    unittest.main()
