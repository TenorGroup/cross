"""X4 Pro: a slider dialog (Sleep timeout, Go to %) has no "- + Confirm" row; a tap or a drag on the bar is the answer.

It stands over the bar at the foot in the panel frame; "<" on the bar or a tap outside cancels.
Runs the X4 Pro simulator (pio run -e simulator_x4pro). X4PRO_PROGRAM picks another build.
"""
import tempfile
from pathlib import Path

from test_thanh_day import run, ink, BAR_TOP, TABS_X
from test_hop_chon import PANEL_FOOT
from test_hop_hoi import stands_over_bar

BAR_Y = BAR_TOP + 30
# The Sleep group's Sleep timeout row (its value at the row's end) and the slider band of its dialog.
TIMEOUT_VALUE = (300, 462, 450, 500)
SLIDER_Y = 668


def main():
    with tempfile.TemporaryDirectory(prefix='x4pro-slide-') as tmp:
        folder = Path(tmp)
        group, dialog, after = run(folder, f'3000:TAP:{TABS_X[4]},{BAR_Y};4500:TAP:240,657;6500:TAP:240,477;'
                                           f'9000:TAP:420,{SLIDER_Y}', [6300, 8500, 11000])
        assert 'Entering activity: SleepTimeoutInterval' in (folder / 'simulator.log').read_text(), 'fixture never opened'
        assert stands_over_bar(dialog), 'the slider dialog does not stand over the bar'
        # No button row under the bar: the band between the slider and the frame's foot is empty.
        assert ink(dialog, (40, SLIDER_Y + 32, 440, PANEL_FOOT - 4)) == 0, 'a button row under the slider'
        # A tap on the bar sets the value and closes the dialog: the row shows the new value.
        assert not stands_over_bar(after), 'the dialog stayed open after a tap on the bar'
        assert list(group.crop(TIMEOUT_VALUE).getdata()) != list(after.crop(TIMEOUT_VALUE).getdata()), \
            'the tap on the bar did not set the value'
    print('GREEN: X4 Pro slider dialogs answer with the bar alone')


if __name__ == '__main__':
    main()
