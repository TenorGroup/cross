import argparse
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--repo', type=Path, required=True)
args = parser.parse_args()
notebook = (args.repo / 'src/shells/ugly/UglyNotebook.cpp').read_text()
screen = (args.repo / 'src/shells/ugly/UglyScreen.cpp').read_text()
header = (args.repo / 'src/shells/ugly/UglyScreen.h').read_text()
assert 'case Key::ConfirmHold:' in notebook, 'RED: recent hold still opens the book'
assert 'OptionPopup' in (args.repo / 'src/shells/ugly/UglyNotebook.h').read_text()
assert 'showRecentActions(cur)' in notebook
assert 'handleModalInput()' in screen
assert 'wasLongPressed(Button::Confirm, HOLD_MS)' in screen
assert 'HOLD_MS = 700' in header
assert 'Task::Forget' in notebook
assert 'ConfirmationActivity' in notebook and 'STR_REMOVE_RECENT_ACTION' in notebook
assert 'RECENT_BOOKS.removeByPath(path)' in notebook
print('GREEN: X3 700 ms hold, shared popup/confirmation and X4 row action routes')
