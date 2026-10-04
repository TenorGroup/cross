"""v1.0.14 Recent entries saved before the thumbnail rename move to the new thumbnail name.

A Recent entry keeps the cover path it was given when the book was last opened. Entries written by
an earlier version point at `thumb_[HEIGHT].bmp` or `thumb2_[HEIGHT].bmp`, the old shapes.
Loading the list now points those entries at `thumb3_[HEIGHT].bmp`; Home builds the missing
thumbnail and the card draws it 1:1.
"""
import json
import os
import subprocess
import unittest
from pathlib import Path

import test_card_cover_shape_v1014 as shape
from test_card_cover_shape_v1014 import CARD_BUILD, PATH, PROGRAM, write_book


class CardCoverRenameTest(unittest.TestCase):
    # Borrow the setup and the open-and-close run without inheriting the other file's tests.
    setUp = shape.CardCoverShapeTest.setUp
    home_after_reading = shape.CardCoverShapeTest.home_after_reading

    def home_only(self):
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(self.sd),
                   CROSSPOINT_SIM_INPUT_SCRIPT='9000:QUIT')
        run = subprocess.run([str(PROGRAM)], cwd=Path(__file__).resolve().parents[2], env=env,
                             capture_output=True, text=True, timeout=120)
        log = run.stdout + run.stderr
        self.assertEqual(run.returncode, 0, log[-6000:])
        return log

    def check_old_thumbnail_name_moves_to_the_new_one(self, old_prefix):
        write_book(self.sd / PATH.lstrip('/'), False)
        self.home_after_reading('doi-ten-lan-1')
        new = sorted(self.store.glob('epub_*/thumb3_*.bmp'))
        self.assertTrue(new, 'the first run wrote no thumbnail')
        cache = new[0].parent
        # Leave the cache as an earlier version left it: only old-name thumbnails, and a Recent entry
        # that points at them.
        for thumb in new:
            thumb.rename(cache / thumb.name.replace('thumb3_', old_prefix))
        for card in cache.glob('*.card*'):
            card.unlink()
        recent = json.loads((self.store / 'recent.json').read_text(encoding='utf-8'))
        recent['books'][0]['coverBmpPath'] = '/' + str(cache.relative_to(self.sd)) + '/' + old_prefix + '[HEIGHT].bmp'
        (self.store / 'recent.json').write_text(json.dumps(recent), encoding='utf-8')

        log = self.home_only()
        self.assertTrue(CARD_BUILD.findall(log), 'the card was not built\n' + log[-4000:])
        self.assertTrue((cache / 'thumb3_450.bmp').exists(),
                        'the card kept the old thumbnail name\n' + log[-4000:])

    def test_old_thumbnail_name_moves_to_the_new_one(self):
        self.check_old_thumbnail_name_moves_to_the_new_one('thumb_')

    def test_second_thumbnail_name_moves_to_the_new_one(self):
        self.check_old_thumbnail_name_moves_to_the_new_one('thumb2_')


if __name__ == '__main__':
    unittest.main()
