"""X4 Pro, tenor/ugly shell: the dynamic bar keeps to the paper.

1. A handwritten Settings form has its own foot ("back to Settings", the sheet number): the bar adds "<" in pen
   only, no tenor/cross ring around it and no zone icon beside it (x 84-144).
2. A screen the ugly shell still draws in tenor/cross (Language, opened from the notebook's Settings page)
   shows the zone it belongs to: the Settings icon, the same pixels as in tenor/cross, not the Recent clock.
X4PRO_PROGRAM selects the X4 Pro simulator.
"""
import sys
from pathlib import Path

from test_thanh_day import PROGRAM

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'reading_stats_simulator'))
import ugly_common
from ugly_common import Card

RING = (16, 724, 76, 727)     # the top edge of the tenor/cross "<" ring, a dotted grey row
BACK = (16, 724, 76, 784)
ZONE = (84, 724, 144, 784)


def ink(image, box):
    pixels = list(image.crop(box).getdata())
    return sum(1 for p in pixels if p == 0) / len(pixels)


def shoot(shell, script, at):
    card = Card(shell=shell)
    try:
        log, images = card.run(script + f';{at + 600}:QUIT', [(at, 'shot')], timeout=120)
    finally:
        card.close()
    return log, images['shot']


def main():
    assert PROGRAM.exists(), f'X4 Pro simulator is required: {PROGRAM}'
    ugly_common.PROGRAM = PROGRAM
    log, form = shoot(1, '2000:TAP:60,750;3500:TAP:200,240', 5500)
    assert '[UGLY] Settings form' in log, 'the journey never opened the handwritten Settings form'
    assert ink(form, BACK) > 0.02, 'no "<" at the foot of the form'
    assert ink(form, RING) < 0.2, 'the "<" of the form is the tenor/cross ring, not pen'
    assert ink(form, ZONE) == 0, 'a zone icon stands on the form\'s own foot'

    log, ugly = shoot(1, '2000:TAP:60,750;3500:TAP:333,558;5000:TAP:292,200', 7500)
    assert 'Entering activity: LanguageSelect' in log, 'the ugly journey never reached Language'
    log, cross = shoot(0, '2000:TAP:423,754;3500:TAP:240,480;5000:TAP:240,68', 7500)
    assert 'Entering activity: LanguageSelect' in log, 'the tenor/cross journey never reached Language'
    assert list(ugly.crop(ZONE).getdata()) == list(cross.crop(ZONE).getdata()), \
        'Language under the ugly Settings page shows another zone icon than Settings'
    print('GREEN: X4 Pro ugly shell: pen "<" alone on paper, the right zone icon on tenor/cross screens')


if __name__ == '__main__':
    main()
