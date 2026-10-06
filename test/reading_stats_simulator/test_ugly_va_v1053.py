"""tenor/ugly on the X3, the ugly screens found in the 12dabc70 pass (v1.0.53): each check names the screen it reads.

Every check drives the simulator from the diary by keys to one screen, takes the frame SETTLE ms after the last key and
reads either the pixels or the `part=` lines the simulator logs when a part draws by hand. UGLY_SHOTS keeps the frames.
"""
import re
import unittest

from PIL import ImageFilter

from ugly_common import Card, digest, ink

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


def ink_rows(image, box):
    """The first and the last row of `box` that hold black."""
    part = image.crop(box)
    rows = [y for y in range(part.height) if ink(part, (0, y, part.width, y + 1))]
    return (rows[0], rows[-1]) if rows else (0, 0)


class Rows(unittest.TestCase):
    def test_a_label_and_a_value_that_do_not_fit_side_by_side_take_2_lines(self):
        # KOReader Sync: "URL server đồng bộ" with the value "Mặc định: kosync.tenor.vn". The layout fits them on one
        # line in the UI font; in hand they touched, no air between. The value goes under the label.
        log, frame, _ = frames(settings_question(9, 1))
        self.assertIn('KOReaderSettings', re.findall(r'Entering activity: (\w+)', log), log[-1500:])
        first, last = ink_rows(frame, (0, 244, 528, 318))
        self.assertGreater(last - first, 40, 'the label and the value of the URL row share one line')


    def test_the_cursor_on_a_locked_row_keeps_its_circle(self):
        # Clock with the automatic time zone: "Múi giờ" is locked, and it is where the cursor starts. The circle goes
        # round its label as round any other; with the cursor on the row under it the label stands alone.
        keys = settings_question(6, 1)
        on = frames(keys)[1]
        below = frames(keys + ['RIGHT'])[1]
        label = (20, 128, 250, 147)  # over the words, where only the circle reaches
        self.assertGreater(ink(on, label), ink(below, label) + 150, 'the cursor on the locked row has no circle')


def firmware_file(card):
    (card.sd / 'fw.bin').write_bytes(b'x' * 64)


class Straight(unittest.TestCase):
    # The keys of the keyboard and the reason a firmware update failed are written in straight letters at both levels
    # of ugliness, 1 turning every other letter. The digests are those of 12dabc70, where a scope that set the level to
    # 0 while it drew did it; a letter turned at level 1 changes them.
    KEYBOARD = {1: '3e483c0b76834935561b5ff4b262057eadc5cf632c73afd4479119dae3184c0a',
                0: '98a2bc72a1dcfa1070a4d6973cd0d323be5ea472376a45af86a7724365e78c16'}
    FIRMWARE = {1: 'be9e2806a00160630e5e5d4e69cec0560d97bf0a2e2ff6b9a9f9a13cbb065652',
                0: 'c8275dd43a000e5826487dd20b6c22cba6b37ba2be8e51cfe2146e564aebf514'}

    def test_the_keys_of_the_keyboard_are_straight_at_both_levels(self):
        for level, golden in self.KEYBOARD.items():
            frame = frames(settings_question(7, 2), uiUglyLevel=level)[1]
            self.assertEqual(digest(frame.crop((0, 300, 528, 590))), golden, 'keys at level %d' % level)

    def test_the_reason_a_firmware_update_failed_is_straight_at_both_levels(self):
        keys = ['UP'] + ['RIGHT'] * 9 + ['CONFIRM'] + ['RIGHT'] * 5 + ['CONFIRM', 'CONFIRM']
        for level, golden in self.FIRMWARE.items():
            frame = frames(keys, firmware_file, uiUglyLevel=level)[1]
            self.assertEqual(digest(frame.crop((0, 380, 528, 480))), golden, 'message at level %d' % level)


class TipBlock(unittest.TestCase):
    # Where the block of tips stands over the key bar is one decision, and the keyboard and the reading habits ask for it.
    # The digests are those of the build before the 2 screens asked: a block a pixel off changes them.
    URL = ['UP'] + ['RIGHT'] * 9 + ['CONFIRM', 'RIGHT', 'CONFIRM', 'CONFIRM', 'RIGHT', 'CONFIRM']

    def test_the_tips_of_the_keyboard_stand_where_they_stood(self):
        # 5 tips under the keys of a name; 6 under the keys of a URL, one more than the room, so the title goes.
        plain = frames(settings_question(7, 2))[1]
        self.assertEqual(digest(plain.crop((0, 590, 528, 752))), '7e5af6dae643c857d26fefba2d09bd278b722a8e7f082dfb2d08e75d219d8558')
        url = frames(self.URL)[1]
        self.assertEqual(digest(url.crop((0, 590, 528, 752))), '629bca9040f44a32a89a3f3a358a86d6c1e0e1835dcae03830a69858db96f84e')

    def test_the_tip_and_the_period_of_the_reading_habits_stand_where_they_stood(self):
        habits = frames(['DOWN'] * 4 + ['CONFIRM'])[1]
        self.assertEqual(digest(habits.crop((0, 620, 528, 752))), 'cf07694cd610ec3237e787e3b7d739e145f6df08951584b7533c782a8a6abc30')


class Header(unittest.TestCase):
    def headers(self, keys):
        log, _, _ = frames(keys)
        return [(p.strip(), t.strip()) for p, t in re.findall(r'part=header prefix=(.*) title=(.*)', log)]

    def test_the_names_a_screen_came_from_are_cut_by_whole_names(self):
        # Clock, 2 names deep ("Cài đặt/Hệ thống"): the room before its title holds the first name and an ellipsis, not
        # "Cài đặt/Hệ th" cut inside a name by the scrawl. The KOReader page ("Cài đặt/Khác") fits and keeps both names.
        deep = self.headers(settings_question(6, 1))
        self.assertIn(('Cài đặt/…/', 'Đồng hồ'), deep)
        self.assertFalse([p for p, _ in deep if '' in p], 'the names before the title end in the scrawl: %s' % deep)
        self.assertIn(('Cài đặt/Khác/', 'Đồng bộ KOReader'), self.headers(settings_question(9, 1)))


if __name__ == '__main__':
    unittest.main()
