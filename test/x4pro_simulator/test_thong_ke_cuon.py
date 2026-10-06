"""X4 Pro Stats: the panel and the rows scroll as one page between the status strip and the dynamic bar
(founder 06/10). Scrolled to the end, the list's frame closes 8 px above the bar and no row shows outside
a frame; the 3 frames are every X4 Pro list's frame (x 16, grey dots 2 px); a tap takes only a row wholly in view; the page keeps its
place across a tab change and across a screen opened from Stats, and starts over after a screen opened
from another tab (the tabs' shared navigation memory); the scroll bar is a pill in the screen's right
margin. Runs the X4 Pro simulator (pio run -e simulator_x4pro). X4PRO_PROGRAM picks another build."""
import tempfile
from pathlib import Path

from test_thanh_day import run, BAR_TOP, TABS_X
from test_thanh_dong import fresh, capture

BAR_Y = BAR_TOP + 30
STATS, RECENT, FOLDER = TABS_X[3], TABS_X[0], TABS_X[1]
TOP, FLOOR = 32, BAR_TOP - 8           # the page's view: under the status strip, 8 px above the bar
FLICKS = ';'.join(f'{5000 + i * 1200}:SWIPE:240,640,240,300,150' for i in range(3))
BACK = '46,754'                        # the bar's "<" on a screen below Home
VIEW = (0, TOP, 456, FLOOR)            # the page, without the scroll bar's margin


def dark(image, x, y):
    return image.getpixel((x, y)) < 128


def rules(image):
    """Rows of the page drawn across the frames' width: frame tops and bottoms, rules between rows."""
    return [y for y in range(TOP, BAR_TOP) if sum(dark(image, x, y) for x in range(60, 420)) > 150]


def frame_bottom(image):
    return max(rules(image))


def same(a, b, box=VIEW):
    return a.crop(box).tobytes() == b.crop(box).tobytes()


def check_end(end):
    bottom = frame_bottom(end)
    assert bottom == FLOOR - 1, f'the list frame closes at y {bottom}, not {FLOOR - 1} (8 px above the bar)'
    poke = [y for y in range(bottom + 1, BAR_TOP) if any(dark(end, x, y) for x in range(0, 480))]
    assert not poke, f'ink between the list frame and the bar (a row out of its frame) at y {poke[:4]}'
    last_rule = max(y for y in rules(end) if y < bottom - 1)
    assert any(dark(end, x, y) for x in range(36, 400) for y in range(last_rule + 4, bottom - 4)), \
        'the last row is not drawn inside the frame'
    # The 3 frames: every list's frame, its sides x 16..17 and 462..463 in grey dots, nothing beside them.
    for y in (bottom - 460, bottom - 20):  # the habits panel's straight sides, the rows' frame
        left = [x for x in range(10, 30) if dark(end, x, y)]
        right = [x for x in range(450, 470) if dark(end, x, y)]
        assert left and set(left) <= {16, 17} and right and set(right) <= {462, 463}, \
            f'frame at y {y} is not the standard frame: sides at {left} and {right}'


def scroll_bar(image):
    """Ink per row in the right margin, outside every frame."""
    rows = {}
    for y in range(TOP, FLOOR):
        n = sum(dark(image, x, y) for x in range(465, 478))
        if n:
            rows[y] = n
    return rows


def check_bar(top, end):
    for name, image, at_end in (('top', top, False), ('end', end, True)):
        rows = scroll_bar(image)
        assert rows, f'no scroll bar in the right margin at the {name}'
        ys = sorted(rows)
        assert ys[0] >= TOP and ys[-1] < FLOOR, f'the scroll bar leaves the page view at the {name}: {ys[0]}..{ys[-1]}'
        solid = [y for y in ys if sum(image.getpixel((x, y)) == 0 for x in range(465, 478)) >= 3]
        assert solid, f'no thumb at the {name}'
        middle = max(rows[y] for y in solid)
        assert rows[solid[0]] < middle and rows[solid[-1]] < middle, f'the thumb ends are square at the {name}'
        if at_end:
            assert solid[-1] > FLOOR - 20, f'the thumb is not at the foot at the end: {solid[-1]}'
        else:
            assert solid[0] < TOP + 20, f'the thumb is not at the head at the top: {solid[0]}'
    # The margin between the frames' right side (x 463) and the bar (x 470) stays white.
    assert not any(dark(end, x, y) for x in range(464, 469) for y in range(TOP, FLOOR)), 'the scroll bar touches a frame'


FAILED = []


def check(name, fn, *args):
    """One rule; a red run names every rule it breaks."""
    try:
        fn(*args)
    except AssertionError as error:
        FAILED.append(f'{name}: {error}')


def expect(name, ok, message):
    if not ok:
        FAILED.append(f'{name}: {message}')


def log(folder):
    return (folder / 'simulator.log').read_text()


# tenorchrome::touchBarTop: 800 - 16 - tabHeight(); the bar grows with the text tier.
TIER_BAR_TOP = {1: 717, 2: 712}


def check_tier(end, tier):
    bar_top = TIER_BAR_TOP[tier]
    bottom = max(y for y in rules(end) if y < bar_top - 1)
    assert bottom == bar_top - 9, f'the list frame closes at {bottom}, the bar starts at {bar_top}'
    poke = [y for y in range(bottom + 1, bar_top) if any(dark(end, x, y) for x in range(0, 480))]
    assert not poke, f'ink between the list frame and the bar at y {poke[:4]}'


def main():
    with tempfile.TemporaryDirectory(prefix='x4pro-stats-') as tmp:
        # A: the end of the page, a tab change and back, then a tap on the last row (wholly in view).
        folder = fresh(tmp, 'a')
        script = (f'3000:TAP:{STATS},{BAR_Y};{FLICKS};9500:TAP:{RECENT},{BAR_Y};11000:TAP:{STATS},{BAR_Y};'
                  f'13000:TAP:200,{FLOOR - 32}')
        top, end, back = run(folder, script, [4500, 9000, 12500], settings=dict(uiTextSize=0))
        capture(folder, ['top', 'end', 'tab-back'], [top, end, back])
        check('end', check_end, end)
        check('scroll bar', check_bar, top, end)
        expect('scroll', not same(top, end), 'the page did not scroll')
        expect('tab memory', same(end, back), 'a tab change lost the place of the page')
        expect('tap', 'Entering activity: Confirmation' in log(folder),
               'a tap on the last row, wholly in view, opened nothing')

        # B: the row the view cuts takes no tap; back from a screen opened from Stats keeps the place.
        folder = fresh(tmp, 'b')
        flicks = ';'.join(f'{6500 + i * 1200}:SWIPE:240,640,240,300,150' for i in range(3))
        script = (f'3000:TAP:{STATS},{BAR_Y};4500:TAP:200,{FLOOR - 8};{flicks};'
                  f'10500:TAP:200,{FLOOR - 1 - 5 * 62 + 31};13000:TAP:{BACK}')
        at_top, end_b, child, again = run(folder, script, [6000, 10000, 12500, 15000], settings=dict(uiTextSize=0))
        capture(folder, ['cut-tap', 'end', 'child', 'back'], [at_top, end_b, child, again])
        text = log(folder)
        opened = 'Entering activity: BookStatsLibrary' in text
        expect('tap', opened, 'a tap on "By book", wholly in view, opened nothing')
        before = text.split('Entering activity: BookStatsLibrary')[0]
        expect('cut row', before.count('Entering activity:') == before.count('Entering activity: Boot') +
               before.count('Entering activity: Home'),
               'a tap on the row the view cuts opened a screen')
        expect('child memory', opened and same(end_b, again), 'back from a screen opened from Stats lost the place')

        # C: a screen opened from another tab: the page starts over (the other tabs are forgotten).
        folder = fresh(tmp, 'c')
        script = (f'3000:TAP:{STATS},{BAR_Y};{FLICKS};9500:TAP:{FOLDER},{BAR_Y};11000:TAP:240,92;'
                  f'13500:TAP:{BACK};15500:TAP:{STATS},{BAR_Y}')
        top_c, end_c, after = run(folder, script, [4500, 9000, 17000], settings=dict(uiTextSize=0))
        capture(folder, ['top', 'end', 'after'], [top_c, end_c, after])
        opened = 'Entering activity: FileBrowser' in log(folder)
        expect('fixture', opened, 'the folder never opened')
        expect('forget', opened and same(top_c, after), 'after a screen opened from another tab the page kept its place')

        # The larger text tiers: the same end.
        for tier in (1, 2):
            folder = fresh(tmp, f'tier{tier}')
            top_t, end_t = run(folder, f'3000:TAP:{STATS},{BAR_Y};{FLICKS}', [4500, 9000], settings=dict(uiTextSize=tier))
            capture(folder, ['top', 'end'], [top_t, end_t])
            check(f'tier {tier}', check_tier, end_t, tier)
    if FAILED:
        raise SystemExit('RED\n' + '\n'.join(FAILED))
    print('GREEN: X4 Pro Stats scrolls as one page: frame closes 8 px above the bar, rows in frames, '
          'taps on whole rows, place kept by the tab memory, pill scroll bar in the right margin')


if __name__ == '__main__':
    main()
