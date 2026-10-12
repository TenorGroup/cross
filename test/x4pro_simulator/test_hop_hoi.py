"""X4 Pro: a question (Confirmation, Clear cache) stands over the bar at the foot, in the panel frame, no black block.

It offers its one action ("Delete", "Clear"); "<" on the bar or a tap outside cancels, so there is no "Cancel"
block. Clear cache used to draw its warning across the screen edges and cut it with the dialog ("Thao ta...").
Runs the X4 Pro simulator (pio run -e simulator_x4pro). X4PRO_PROGRAM picks another build.
"""
import json
import sys
import tempfile
from pathlib import Path

from test_thanh_day import run, ink, BAR_TOP, TABS_X
from test_hop_chon import black_rows, PANEL_FOOT
from test_thanh_ngang import bookmark

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'reading_stats_simulator'))
from test_quotes_v1011 import write_quote  # noqa: E402

BAR_Y = BAR_TOP + 30


def frame_width(image):
    """How wide the question's frame stands, read off its bottom edge over the bar."""
    box = image.crop((0, PANEL_FOOT - 16, 480, PANEL_FOOT)).point(lambda v: 255 if v < 200 else 0).getbbox()
    return box[2] - box[0] if box else 0


def one_quote(sach):
    quotes = sach.parent / '.crosspoint/quotes'
    quotes.mkdir(parents=True)
    (quotes / '.ten-v2').write_text('2')
    write_quote(quotes, '/sach/test_kerning_ligature.epub', 'test_kerning_ligature',
                'Sương còn đọng trên lá khi người hái chè lên tới đỉnh đồi.', 0, 1, 20260919, 1290)


def stands_over_bar(image):
    """The frame's grey dotted bottom edge 8 px over the bar, between its round corners (the frame takes the width
    its words ask, so the edge is read where the frame stands)."""
    box = image.crop((0, PANEL_FOOT - 2, 480, PANEL_FOOT)).point(lambda v: 255 if v < 128 else 0).getbbox()
    return bool(box) and box[2] - box[0] > 60 and 0.35 < ink(image, (box[0] + 24, PANEL_FOOT - 2, box[2] - 24, PANEL_FOOT)) < 0.65


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
        # The frame is as wide as its caption and warning ask, up to the screen's room (448 px): they wrap by word,
        # never a letter or a syllable to a line.
        assert frame_width(cache) >= 440, ('the Clear cache question is narrower than its warning', frame_width(cache))

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
        to_ask = ('3000:TAP:240,300;7000:TAP:240,775;8500:TAP:131,754;10000:TAP:240,433;12500:TAP:240,110,900;'
                  '15000:TAP:380,290')  # Delete, in the menu held at the row's right end
        (bm_ask,) = run(f3, to_ask, [17000], settings={'readerFavorites': [9], 'readerTapTip': 0}, write_books=bookmark)
        assert 'Entering activity: EpubReaderBookmarks' in (f3 / 'simulator.log').read_text(), 'fixture never reached Bookmarks'
        assert stands_over_bar(bm_ask), 'the delete bookmark question does not stand over the bar'
        assert ink(bm_ask, (30, 140, 200, 260)) == 0, 'the delete bookmark question still hangs from the row'
        # "Xóa dấu trang này?" on one line: the frame takes its caption's width (about 230 px) and its margins.
        assert frame_width(bm_ask) >= 250, ('the delete bookmark question wraps its caption', frame_width(bm_ask))
        f4 = Path(tmp) / 'bookmark-yes'
        f4.mkdir()
        run(f4, to_ask + f';18000:TAP:240,{PANEL_FOOT - 30}', [20000],
            settings={'readerFavorites': [9], 'readerTapTip': 0}, write_books=bookmark)
        saved = json.loads((f4 / 'sd/.crosspoint/bookmarks/sach_test_kerning_ligature.json').read_text())
        assert saved['bookmarks'] == [], 'the question\'s one action did not delete the bookmark'

        # Statistics > Quotes > the book > the quote > its menu > Delete: "Xóa trích dẫn này?" on one line over
        # Cancel and Confirm (the frame used to take the width of "Xác nhận" and set the caption a word to a line).
        f5 = Path(tmp) / 'quote'
        f5.mkdir()
        (quote_ask,) = run(f5, f'3000:TAP:{TABS_X[3]},{BAR_Y};4500:SWIPE:240,600,240,250,150;6500:TAP:240,558;'
                               '8500:CONFIRM;10500:CONFIRM;12500:CONFIRM;14500:DOWN;15500:CONFIRM', [17500],
                           write_books=one_quote, sim_env={'CROSSPOINT_SIM_NOW': '1790000000'})
        assert 'Quote delete asks' in (f5 / 'simulator.log').read_text(), 'fixture never asked to delete the quote'
        assert stands_over_bar(quote_ask), 'the delete quote question does not stand over the bar'
        assert frame_width(quote_ask) >= 240, ('the delete quote question wraps its caption', frame_width(quote_ask))

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
