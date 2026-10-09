#!/usr/bin/env python3
"""Verify the X4 Pro text-settings bar and reader entry flow."""

import argparse
from pathlib import Path
import subprocess


def function(source: str, signature: str) -> str:
    begin = source.index(signature)
    opening = source.index("{", begin)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[begin:end]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cxx", default="c++")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    ui_source = (args.repo / "src/activities/UiTabListActivity.cpp").read_text()
    text_header = (args.repo / "src/activities/settings/TextSettingsActivity.h").read_text()
    text_source = (args.repo / "src/activities/settings/TextSettingsActivity.cpp").read_text()
    reader_source = (args.repo / "src/activities/reader/EpubReaderActivity.cpp").read_text()

    height = function(ui_source, "int touchTabBarHeight(")
    chrome = (args.repo / "src/components/TenorMenuChrome.h").read_text()
    rect_type = function(chrome, "struct TouchBarRect") + ";"
    rect = function(chrome, "inline TouchBarRect touchTabBarRect(")
    harness = f"""
#include <initializer_list>
namespace tenorchrome {{
constexpr int TOUCH_BAR_BOTTOM_GAP = 16;
constexpr int FOOT_BACK_X = 16;
constexpr int FOOT_BACK_SIZE = 60;
{rect_type}
{rect}
}}
{height}
int main() {{
  for (const int regular : {{50, 60, 70}})
    if (touchTabBarHeight(true, regular) != 60) return 1;
  if (touchTabBarHeight(false, 47) != 47) return 2;
  const auto bar = tenorchrome::touchTabBarRect(480, 800);
  if (bar.x != 16 || bar.y != 724 || bar.width != 448 || bar.height != 60) return 3;
  return 0;
}}
"""
    harness_path = args.output / "geometry.cpp"
    binary = args.output / "geometry"
    harness_path.write_text(harness)
    subprocess.run([args.cxx, "-std=c++20", "-Wall", "-Wextra", "-Werror",
                    str(harness_path), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)

    tab_count = function(text_header, "int tabCount() const override")
    require("x3ScreenLayoutEnabled() ? 5 : 4" in tab_count,
            "Text settings must keep 5 X3 tabs and expose 4 X4 Pro tabs")
    tab_icon = function(text_source, "freeink::ui::BitmapRef TextSettingsActivity::tabIcon(")
    require("index < 0 || index >= tabCount()" in tab_icon,
            "Text settings icons must reject only invalid tab indices")
    require("!tenorchrome::x3ScreenLayoutEnabled()" not in tab_icon,
            "X4 Pro text tabs must expose icons")
    require("icon_tenor_home_favorites_40" in tab_icon and
            "icon_tenor_home_favorites_bold_40" in tab_icon,
            "Text tabs must use the existing icon identifiers until icon assets land")

    build_bar = function(ui_source, "void UiTabListActivity::buildTabBar(")
    require("tenorchrome::touchTabBarRect(frameRect" in build_bar,
            "The shared tab activity must own the touch bar rectangle")
    require("tenorchrome::drawBarTab" in ui_source,
            "The shared tab activity must draw touch icons through drawBarTab")

    reader_menu = function(reader_source, "void EpubReaderActivity::onReaderMenuConfirm(")
    require("std::make_unique<TextSettingsActivity>" in reader_menu and
            "&sdFontSystem.registry()" in reader_menu,
            "Reader text settings must push the shared activity with the font registry")
    require("const AnhChupChu truoc = AnhChupChu::chup();" in reader_menu and
            "if (truoc != AnhChupChu::chup()) danLaiTrang();" in reader_menu and
            "inkTruoc != SETTINGS.readerInkWeight || aaTruoc != SETTINGS.textAntiAliasing" in reader_menu and
            "readerInk::apply(renderer);" in reader_menu and
            "discardOverlayPage();" in reader_menu and "requestUpdate();" in reader_menu,
            "Reader text settings must keep the existing return invalidation contract")

    overlay_input = function(reader_source, "void EpubReaderActivity::handleOverlayInput(")
    require("const auto openTextSettings" in overlay_input,
            "The toolbar needs one shared TextSettingsActivity entry")
    require(overlay_input.count("openTextSettings();") >= 3,
            "Touch, button and panel tool routes must use the shared Text settings entry")
    open_text = function(overlay_input, "const auto openTextSettings")
    require("closeOverlayToPage();" not in open_text,
            "Opening Text settings must not repaint the reader page first")
    for statement in ("RenderLock lock;", "overlay = Overlay::None;", "overlayPopup.dismiss();",
                      "toolbarUi.reset();", "settleOverlayRefresh();", "discardOverlayPage();",
                      "panelClosedLocked(false, false);",
                      "onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction::TEXT_SETTINGS, MenuResult{})"):
        require(statement in open_text, f"Text settings transition is missing: {statement}")
    require("displayBuffer" not in open_text and "requestUpdate" not in open_text,
            "Text settings transition must not queue a reader frame")

    reader_loop = function(reader_source, "void EpubReaderActivity::loop()")
    text_zone = reader_loop[reader_loop.index("case readertap::Zone::TextMenu:"):
                            reader_loop.index("default:", reader_loop.index("case readertap::Zone::TextMenu:"))]
    require("#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO" in text_zone and
            "#else" in text_zone and "#endif" in text_zone,
            "The page Text gesture needs an explicit X4 Pro compile guard")
    require("onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction::TEXT_SETTINGS, MenuResult{})" in text_zone,
            "The page Text gesture must open the shared TextSettingsActivity")
    require("focusedTool = kTextTool;" in text_zone and "openOverlay(Overlay::Text);" in text_zone,
            "The non-X4 Pro Text gesture must keep its original overlay path")

    print("text settings touch geometry and reader flow: PASS")


if __name__ == "__main__":
    main()
