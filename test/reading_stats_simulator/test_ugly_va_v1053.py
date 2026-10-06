"""tenor/ugly on the X3, the ugly screens found in the 12dabc70 pass (v1.0.53): each check names the screen it reads.

Every check drives the simulator from the diary by keys to one screen, takes the frame SETTLE ms after the last key and
reads either the pixels or the `part=` lines the simulator logs when a part draws by hand. UGLY_SHOTS keeps the frames.
"""
import re
import unittest

from PIL import ImageFilter

from ugly_common import Card

GAP, START, SETTLE = 700, 1500, 1800


def settings_question(group, question):
    """Keys from the diary to question `question` of Settings group `group` (both counted from 1, groups from 0)."""
    return ['UP'] + ['RIGHT'] * group + ['CONFIRM'] + ['RIGHT'] * (question - 1) + ['CONFIRM']


def frames(keys, prepare=None, at=(), **settings):
    """Drives the diary by `keys` (WAIT:ms stands for a pause); returns (log, frame, extra frames at `at` ms after the last key)."""
    card = Card(**settings)
    try:
        if prepare:
            prepare(card)
        t, parts = START, []
        for k in keys:
            if k.startswith('WAIT:'):
                t += int(k[5:])
                continue
            parts.append('%d:%s' % (t, k))
            t += GAP
        shot = t - GAP + SETTLE
        parts.append('%d:QUIT' % (shot + max([600] + [a + 600 for a in at])))
        log, shots = card.run(';'.join(parts), [(shot, 'frame')] + [(shot + a, 'later%d' % a) for a in at], timeout=120)
    finally:
        card.close()
    assert 'frame' in shots, log[-1500:]
    return log, shots['frame'], [shots['later%d' % a] for a in at]


def solid_blocks(image, box, size=6):
    """How many pixels of `box` are the centre of a size x size block of black (a pen stroke is 2 or 3 px wide)."""
    part = image.crop(box).convert('L').filter(ImageFilter.MaxFilter(size | 1))
    return sum(1 for p in part.getdata() if p == 0)


def folder(card):
    (card.sd / 'Truyện ngắn').mkdir()
    (card.sd / 'Truyện ngắn' / 'a.txt').write_text('x' * 100)


class KeyMarks(unittest.TestCase):
    def test_the_tip_of_the_file_browser_writes_its_side_buttons_by_hand(self):
        # A folder of the browser: "Giữ [side] Đọc thử  [side] Xóa" over the key bar. The side buttons were the UI font's
        # solid triangles, laid over the words; they are the pen's marks now, none of them solid.
        log, frame, _ = frames(['DOWN', 'DOWN', 'CONFIRM'], folder)
        self.assertIn('FileBrowser', re.findall(r'Entering activity: (\w+)', log), log[-1500:])
        self.assertEqual(solid_blocks(frame, (0, 672, 528, 740)), 0, 'a solid triangle of the UI font in the tip')
        drawn = set(re.findall(r'part=penmark cp=(\w+)', log))
        self.assertTrue({'E107', 'E108'} <= drawn, 'the side buttons of the tip are not drawn by the pen: %s' % sorted(drawn))

    def test_the_last_tip_of_the_keyboard_writes_its_two_keys_by_hand(self):
        # The keyboard of "Tên thiết bị": "Giữ [Back]: Về Home. Giữ [erase]: Xóa hết". The erase key was the UI font's
        # box laid over the "xóa" after it; it is the pen's mark in its own cell now, the Back mark beside it.
        log, _, _ = frames(settings_question(7, 2))
        self.assertIn('KeyboardEntry', re.findall(r'Entering activity: (\w+)', log), log[-1500:])
        drawn = set(re.findall(r'part=penmark cp=(\w+)', log))
        self.assertTrue({'E101', 'E109'} <= drawn, 'the 2 keys of the last tip are not drawn by the pen: %s' % sorted(drawn))


if __name__ == '__main__':
    unittest.main()
