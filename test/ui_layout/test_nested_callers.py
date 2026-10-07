from pathlib import Path

ROOT = Path(__file__).parents[2]

CASES = {
    "settings": (ROOT / "src/activities/settings/SettingsActivity.cpp", "optionPopup.showInFrame"),
    "home_buttons": (ROOT / "src/activities/settings/HomeButtonSettingsActivity.cpp", "optionPopup.showInFrame"),
    "opds": (ROOT / "src/activities/settings/OpdsServerListActivity.cpp", "optionPopup.showInFrame"),
    "ble": (ROOT / "src/activities/settings/BlePageTurnerActivity.cpp", "optionPopup.showInFrame"),
    "text": (ROOT / "src/activities/settings/TextSettingsActivity.cpp", "optionPopup_.showInFrame"),
    "quote_edit": (ROOT / "src/activities/home/QuoteDetailActivity.cpp", "popup.showInFrame"),
}

for name, (path, call) in CASES.items():
    source = path.read_text()
    assert call in source, f"{name}: picker does not use the parent frame"
    if name != "quote_edit":
        assert "rowFrameFor(ACTION_ROW" in source, f"{name}: picker rebuilds frame geometry"

for path in [ROOT / "src/activities/settings/HomeButtonSettingsActivity.cpp",
             ROOT / "src/activities/settings/OpdsServerListActivity.cpp",
             ROOT / "src/activities/settings/BlePageTurnerActivity.cpp",
             ROOT / "src/activities/settings/TextSettingsActivity.cpp"]:
    source = path.read_text()
    assert "opensNext" in source, f"{path.name}: missing shared chevron cue"

quote = (ROOT / "src/activities/home/QuoteDetailActivity.cpp").read_text()
assert "popup.lastFrame()" in quote, "QuoteDetail: nested Edit does not reuse the context frame"

print("PASS: non-reader pickers use recorded row frames and chevron cues")
