from pathlib import Path

ROOT = Path(__file__).parents[2]

option_popup = (ROOT / "src/components/OptionPopup.h").read_text()
assert "renderer.fillRoundedRect(box.x, box.y, box.width, box.height" in option_popup
assert "tenorchrome::drawRoundRing(renderer, box.x, box.y, box.width, box.height" in option_popup

CASES = {
    "ble": (ROOT / "src/activities/settings/BlePageTurnerActivity.cpp", "optionPopup.showInFrame"),
    "quote_edit": (ROOT / "src/activities/home/QuoteDetailActivity.cpp", "popup.showInFrame"),
}

for name, (path, call) in CASES.items():
    source = path.read_text()
    assert call in source, f"{name}: picker does not use the parent frame"
    if name != "quote_edit":
        assert "rowFrameFor(ACTION_ROW" in source, f"{name}: picker rebuilds frame geometry"

for filename, popup in [("SettingsActivity.cpp", "optionPopup"), ("TextSettingsActivity.cpp", "optionPopup_"),
                        ("HomeButtonSettingsActivity.cpp", "optionPopup"), ("OpdsServerListActivity.cpp", "optionPopup")]:
    source = (ROOT / "src/activities/settings" / filename).read_text()
    assert f"showSettingsChoices({popup}," in source, f"{filename}: missing U12 choice dispatch"
    if filename == "HomeButtonSettingsActivity.cpp":
        assert "rows[i].opensNext = true" in source
    else:
        cues = (ROOT / "src/activities/settings/SettingsActivity.h").read_text() if filename == "SettingsActivity.cpp" else source
        assert "settingsChoiceStyle(" in cues and "SettingsChoiceStyle::Page" in cues
        if filename != "OpdsServerListActivity.cpp":
            assert "!tenorchrome::kTouchShell && settingstabs::moTrinhChon(" in cues
        else:
            assert "format.opensNext = !tenorchrome::kTouchShell ||" in source

parent = (ROOT / "src/activities/UiListActivity.cpp").read_text()
assert "settingsChoiceStyle(static_cast<int>(labels.size()), tenorchrome::kTouchShell) == SettingsChoiceStyle::Page" in parent
assert "std::make_unique<SettingsChoiceActivity>" in parent
assert "popup.show(title, labels, selected, std::move(onSelect))" in parent
assert "if (tenorchrome::kTouchShell) popup.alignValueTo(app.publishedRect(ACTION_ROW, static_cast<int16_t>(row)))" in parent

for path in [ROOT / "src/activities/settings/HomeButtonSettingsActivity.cpp",
             ROOT / "src/activities/settings/OpdsServerListActivity.cpp",
             ROOT / "src/activities/settings/BlePageTurnerActivity.cpp",
             ROOT / "src/activities/settings/TextSettingsActivity.cpp"]:
    source = path.read_text()
    assert "opensNext" in source, f"{path.name}: missing shared chevron cue"

quote = (ROOT / "src/activities/home/QuoteDetailActivity.cpp").read_text()
assert "popup.lastFrame()" in quote, "QuoteDetail: nested Edit does not reuse the context frame"

print("PASS: U11 pickers reuse parent frames; U12 uses aligned popups or child pages with matching cues")
