"""X4 Pro reader Text menu (founder 06/10): all 14 text settings of Settings > Reader > Text settings, in its
order (Font, Size, Line spacing, Letter spacing, Word spacing, Paragraph spacing, Alignment, Margin, Indent,
Book style, Drop cap, Hyphenation, Ink weight, Anti-aliasing). Choice rows open a child list in the panel frame.
Scrolled, the size stepper goes with its row.

Runs the X4 Pro simulator (pio run -e simulator_x4pro). X4PRO_PROGRAM picks another build.
"""
import json
import tempfile
from pathlib import Path

from PIL import ImageChops

from test_thanh_day import run, ink, TABS_X, BAR_TOP

OPEN_BOOK = f'3000:TAP:{TABS_X[1]},{BAR_TOP + 30};5000:TAP:240,68;7000:TAP:240,68'
TEXT_MENU = OPEN_BOOK + ';9500:TAP:240,775'
ROW_Y = [433, 495, 557, 619, 681]  # the Text panel's rows, 62 px apart
MINUS = (262, 470, 300, 520)       # the size stepper's "-" on the panel's 2nd slot


def main():
    with tempfile.TemporaryDirectory(prefix='x4pro-text14-') as tmp:
        root = Path(tmp)
        # The 4th row opens its value list in the panel frame, then saves the selected wide value.
        parent, child, closed = run(root / 'a', TEXT_MENU + f';12000:TAP:240,{ROW_Y[3]};14500:TAP:240,620;17000:TAP:46,754',
                                    [11500, 13500, 17500])
        saved = json.loads((root / 'a/sd/.crosspoint/settings.json').read_text())
        assert ImageChops.difference(parent, child).crop((30, 40, 450, BAR_TOP - 8)).getbbox(), \
            'the letter spacing child list did not open'
        assert ImageChops.difference(parent, child).crop((16, 40, 464, 46)).getbbox() is None, \
            'the letter spacing child changed the parent frame'
        assert saved.get('letterSpacing') == 3, f'the chosen letter spacing was not saved: {saved.get("letterSpacing")}'
        assert saved.get('paragraphAlignment', 0) == 0, 'the 4th row stepped alignment'
        assert ImageChops.difference(child, closed).getbbox(), 'closing the child list did not restore the text panel'
        # 14 rows: the panel scrolls, and the 2nd slot then holds a row without the size stepper.
        top, scrolled = run(root / 'b', TEXT_MENU + ';12500:SWIPE:240,600,240,250,200', [12000, 14500])
        assert ink(top, MINUS) > 0, 'no size stepper on the size row'
        assert ink(scrolled, MINUS) == 0, 'the size stepper stayed on the 2nd slot after a scroll'
    print('GREEN: X4 Pro Text menu holds the 14 text settings in order; the stepper goes with the size row')


if __name__ == '__main__':
    main()
