#!/usr/bin/env python3
from pathlib import Path
import sys


repo = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[2]
home_header = (repo / "src/activities/home/HomeActivity.h").read_text()
home_source = (repo / "src/activities/home/HomeActivity.cpp").read_text()
list_header = (repo / "src/activities/UiListActivity.h").read_text()
list_source = (repo / "src/activities/UiListActivity.cpp").read_text()


def require(text: str, message: str) -> None:
    if text not in home_header and text not in home_source and text not in list_header and text not in list_source:
        raise AssertionError(message)


def function(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for position in range(opening, len(source)):
        if source[position] == "{":
            depth += 1
        elif source[position] == "}":
            depth -= 1
            if depth == 0:
                return source[start : position + 1]
    raise AssertionError(f"Unclosed function: {signature}")


require("showRecentActions", "Recent actions do not share one menu builder")
require("STR_REMOVE_RECENT_ACTION", "Recent actions lack the approved remove label")
require("pendingRecentRemoval", "The menu callback does not defer confirmation")
require("ConfirmationActivity", "Removing a recent book lacks the shared confirmation component")
require("RECENT_BOOKS.removeByPath", "Confirmed removal does not use RecentBooksStore")
require("buttonHoldOpensRowMenu", "UiListActivity lacks the scoped button-hold menu hook")

loop_input = function(list_source, "void UiListActivity::loopInput()")
hold = loop_input.index("mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, 700)")
lock = loop_input.index("RenderLock lock", hold)
row = loop_input.index("const int row", lock)
pin_origin = loop_input.index("const bool pinFromTouch", row)
origin_clear = loop_input.index("pendingPinFromTouch = false", pin_origin)
hook = loop_input.index("buttonHoldOpensRowMenu", origin_clear)
menu = loop_input.index("onRowLongPress", hook)
pin = loop_input.index("toggleFavorite", menu)
if not hold < lock < row < pin_origin < origin_clear < hook < menu < pin:
    raise AssertionError("Button hold does not route to the row menu before the generic pin write")
if "!pinFromTouch" not in loop_input[row:menu]:
    raise AssertionError("Selecting Pin in the popup would reopen the popup")

home_hook = function(home_source, "bool HomeActivity::buttonHoldOpensRowMenu")
if "Tab::RECENT" not in home_hook or "recentBooks.size()" not in home_hook:
    raise AssertionError("Home does not scope the button-hold menu to a valid Recent row")

home_buttons = function(home_source, "bool HomeActivity::handleButtons()")
home_hold = home_buttons.index("mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, 700)")
home_hold_end = home_buttons.index("// Back opens the most recently read book", home_hold)
if "showRecentActions" in home_buttons[home_hold:home_hold_end]:
    raise AssertionError("Home keeps a dead duplicate Recent hold branch after UiListActivity consumes the hold")

for function in ("HomeActivity::onRowLongPress", "HomeActivity::showOtherBookMenu"):
    start = home_source.index(function)
    end = home_source.find("\n}\n", start)
    if "showRecentActions" not in home_source[start:end]:
        raise AssertionError(f"{function} does not use the anchored Recent action menu")

popup_source = (repo / "src/components/OptionPopup.h").read_text()
if "props.styles.normal.radius = static_cast<uint8_t>(metrics.popupCornerRadius);" not in popup_source:
    raise AssertionError("The popup outer frame takes the theme's corner radius on every board")

for language, question in (("vietnamese", "Bỏ khỏi Gần đây?"),
                           ("english", "Remove from Recents?"),
                           ("chinese", "移出最近阅读？")):
    translation = (repo / "lib/I18n/translations" / (language + ".yaml")).read_text()
    if 'STR_REMOVE_FROM_RECENTS: "' + question + '"' not in translation:
        raise AssertionError("Recent confirmation must use the approved remove wording: " + language)

confirmation = (repo / "src/activities/util/ConfirmationActivity.cpp").read_text()
if "if (shell::uglyParts())" in confirmation or confirmation.count("if (shell::isUgly())") < 2:
    raise AssertionError("Every ugly confirmation must use askBox, including touch devices")

print("recent actions contract: ok")
