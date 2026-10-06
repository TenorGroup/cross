"""X4 Pro: a question (Confirmation, Clear cache) stands over the bar at the foot, in the panel frame, no black block.

It offers its one action ("Delete", "Clear"); "<" on the bar or a tap outside cancels, so there is no "Cancel"
block. Clear cache used to draw its warning across the screen edges and cut it with the dialog ("Thao ta...").
Runs the X4 Pro simulator (pio run -e simulator_x4pro). X4PRO_PROGRAM picks another build.
"""
import json
import tempfile
from pathlib import Path

from test_thanh_day import run, ink, BAR_TOP, TABS_X
from test_hop_chon import black_rows, PANEL_FOOT
from test_thanh_ngang import bookmark

BAR_Y = BAR_TOP + 30


def stands_over_bar(image):
    """The frame's grey dotted bottom edge 8 px over the bar."""
    return 0.35 < ink(image, (60, PANEL_FOOT - 2, 420, PANEL_FOOT)) < 0.65


def main():
    with tempfile.TemporaryDirectory(prefix='x4pro-ask-') as tmp:
        # Settings > Other (after a flick) > Clear cache of all books; then a tap above the question cancels.
        f1 = Path(tmp) / 'cache'
        f1.mkdir()
        cache, back = run(f1, f'3000:TAP:{TABS_X[4]},{BAR_Y};4500:SWIPE:240,600,240,250,150;6500:TAP:240,557;'
                              '8500:TAP:240,265;12000:TAP:240,150', [11500, 14500])
        assert stands_over_bar(cache), 'the Clear cache question does not stand over the bar'
        assert not black_rows(cache, 40, PANEL_FOOT), 'a black block in the Clear cache question'
        assert ink(cache, (0, 40, 12, PANEL_FOOT)) == 0 and ink(cache, (468, 40, 480, PANEL_FOOT)) == 0, \
            'text runs past the screen edges'
        log = (f1 / 'simulator.log').read_text()
        assert 'Entering activity: ClearCache' in log, 'fixture never reached Clear cache'
        assert 'Clearing cache' not in log and not stands_over_bar(back), 'a tap outside did not cancel'

        # File > sach/ > hold the first book > Delete: the Confirmation question.
        f2 = Path(tmp) / 'delete'
        f2.mkdir()
        (ask,) = run(f2, f'3000:TAP:{TABS_X[1]},{BAR_Y};5000:TAP:240,98;8000:TAP:240,157,900;10500:TAP:100,336',
                     [12500], extra_books=3)
        assert 'Entering activity: Confirmation' in (f2 / 'simulator.log').read_text(), 'fixture never asked'
        assert stands_over_bar(ask), 'the Confirmation question does not stand over the bar'
        assert not black_rows(ask, 40, PANEL_FOOT), 'a black Cancel block in the Confirmation question'

        # Reader > the foot band > Favourites > Bookmarks > hold the bookmark > Delete: the delete question is a
        # question too, over the bar with its one action (no "Cancel" row); its action deletes.
        f3 = Path(tmp) / 'bookmark'
        f3.mkdir()
        to_ask = ('3000:TAP:240,300;7000:TAP:240,775;8500:TAP:416,754;10000:TAP:240,433;12500:TAP:240,110,900;'
                  '15000:TAP:200,278')
        (bm_ask,) = run(f3, to_ask, [17000], settings={'readerFavorites': [9], 'readerTapTip': 0}, write_books=bookmark)
        assert 'Entering activity: EpubReaderBookmarks' in (f3 / 'simulator.log').read_text(), 'fixture never reached Bookmarks'
        assert stands_over_bar(bm_ask), 'the delete bookmark question does not stand over the bar'
        assert ink(bm_ask, (30, 140, 200, 260)) == 0, 'the delete bookmark question still hangs from the row'
        f4 = Path(tmp) / 'bookmark-yes'
        f4.mkdir()
        run(f4, to_ask + f';18000:TAP:240,{PANEL_FOOT - 30}', [20000],
            settings={'readerFavorites': [9], 'readerTapTip': 0}, write_books=bookmark)
        saved = json.loads((f4 / 'sd/.crosspoint/bookmarks/sach_test_kerning_ligature.json').read_text())
        assert saved['bookmarks'] == [], 'the question\'s one action did not delete the bookmark'

    # The update question: the simulator never gets past Wi-Fi to it, so its call is read. On the X4 Pro it is a
    # question (the headline form, which stands over the bar) offering Update alone, with nothing marked.
    ota = (Path(__file__).resolve().parents[2] / 'src/activities/settings/OtaUpdateActivity.cpp').read_text()
    assert '#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO\n  constexpr int skip = 1, preset = -1;' in ota, \
        'the touch update question keeps Cancel or marks Update'
    assert 'confirmPopup.show(tr(STR_NEW_UPDATE), "", options + skip, 2 - skip, preset,' in ota, \
        'the update question is not the headline form'
    print('GREEN: X4 Pro questions stand over the bar, framed, one action')


if __name__ == '__main__':
    main()
