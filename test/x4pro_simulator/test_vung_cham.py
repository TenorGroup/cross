"""X4 Pro tap zones (founder 05/10): page turns by tap in a 25% back column, the page's top band opens the
top menu, its foot band the text menu, and the status strip opens the top menu on a screen below another."""
from pathlib import Path
import tempfile

from test_thanh_day import run

PAGE = (0, 110, 480, 730)  # between the bands


def same(a, b, box=PAGE):
    return list(a.crop(box).getdata()) == list(b.crop(box).getdata())


def log(folder):
    return (folder / 'simulator.log').read_text()


def turn_case(folder, settings):
    # 150 px from the left edge went back under the old 1/3 split; now it is the forward zone.
    first, forward, back = run(folder, '3000:TAP:240,300;7000:TAP:150,400;9500:TAP:60,400', [6600, 9000, 11500],
                               settings=settings)
    assert 'enter EpubReader ' in log(folder), 'fixture never entered Reader'
    assert not same(first, forward), 'a tap at x=150 did not turn forward'
    assert same(first, back), 'a tap in the back column did not turn back'


def centre_case(folder):
    # The middle of the page turns forward (founder 06/10): no reader menu there any more.
    first, centre, back = run(folder, '3000:TAP:240,300;7000:TAP:240,400;9500:TAP:60,400', [6600, 9000, 11500])
    assert 'enter EpubReader ' in log(folder), 'fixture never entered Reader'
    assert not same(first, centre), 'a tap in the middle did nothing'
    # A turn: the back column brings the first page back (an open menu would have taken that tap).
    assert same(first, back), 'a tap in the middle opened the reader menu instead of turning'


def menu_case(folder):
    # One menu from the foot band (founder 06/10): it opens on its Text tab, the bar beside "<" holds the
    # reader menu's other tabs (contents first), and "<" closes it back to the page.
    page, text, contents, back = run(folder, '3000:TAP:240,300;7000:TAP:240,775;9500:TAP:226,754;12000:TAP:46,754',
                                     [6600, 9000, 11500, 14000])
    assert not same(page, text), 'the foot band opened nothing'
    assert not same(text, contents), 'the contents tab in the bar opened nothing'
    assert same(page, back), '"<" did not close the menu'


def bands_case(folder):
    page, text = run(folder, '3000:TAP:240,300;7000:TAP:240,775', [6600, 8600])
    assert 'enter EpubReader ' in log(folder), 'fixture never entered Reader'
    assert not same(page, text, (0, 300, 480, 800)), 'the foot band opened nothing'
    assert 'enter FrontlightPanel' not in log(folder), 'the foot band opened the top menu'
    run(folder.parent / (folder.name + '-top'), '3000:TAP:240,300;7000:TAP:240,40', [6600, 8600])
    assert 'enter FrontlightPanel' in log(folder.parent / (folder.name + '-top')), 'the top band opened no top menu'
    # The top band is the foot band's 62 px (founder 06/10): y 80 is the page, it turns forward.
    below = folder.parent / (folder.name + '-below')
    run(below, '3000:TAP:240,300;7000:TAP:300,80', [6600, 8600])
    assert 'enter FrontlightPanel' not in log(below), 'y 80 still opened the top menu'


def strip_case(folder):
    # Settings, then its Display group (a screen below another, "<" at the foot), then the strip.
    run(folder, '2000:TAP:423,754;4000:TAP:240,100;6000:TAP:240,100;8000:TAP:240,10', [5800, 7800, 9800])
    assert 'enter FrontlightPanel' in log(folder), 'the strip of a sub-screen opened no top menu'


def main():
    with tempfile.TemporaryDirectory(prefix='x4pro-zones-') as tmp:
        root = Path(tmp)
        turn_case(root / 'fresh', None)
        # A file from before the tap zones that turned by swipe only: moved once, taps turn.
        turn_case(root / 'swipe-only', dict(pageTurnGesture=2, previousPageGesture=2))
        centre_case(root / 'centre')
        menu_case(root / 'menu')
        bands_case(root / 'bands')
        strip_case(root / 'strip')
    print('GREEN: X4 Pro tap zones: 25% back column, old swipe-only file moved, top/foot bands, strip top menu')


if __name__ == '__main__':
    main()
