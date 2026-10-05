"""Recent duration canonical GREEN gate and screenshot capture.

Capture 654 minutes, 999h 59m and a duration beyond the column in all 3 UI tiers.
Requires six complete rows with last-row ink aligned to the cover bottom.
Historical frozen RED captures are archived outside this canonical gate.
"""
import json
import os
import unittest

from PIL import ImageChops

from test_home_recent_v1011 import HomeRecentCardTest
from test_home_card_v1011 import fnv64


class RecentDurationFixture(HomeRecentCardTest):
    def test_duration_columns_three_tiers(self):
        books = [self.add_book('/duration-%d.epub' % i, 'Duration fixture %d' % i, 'Tenor')
                 for i in range(3)]
        self.write_recent(books)
        folder = self.store / 'reading-stats'
        folder.mkdir()
        for i, minutes in enumerate((654, 999 * 60 + 59, 4294967295)):
            record = dict(bookEpoch=0, path=books[i]['path'], title=books[i]['title'],
                          minutes=minutes, ms=0, turns=412, first=20260805, last=20260817,
                          days=6, progress=34, startProgress=0)
            (folder / ('tenor_%016x.json' % fnv64(record['path']))).write_text(json.dumps(record))
        artifacts = self.artifacts
        for tier in range(3):
            (self.store / 'settings.json').write_text(json.dumps(
                dict(language='VI', uiTheme=4, sleepTimeout=120, uiTextSize=tier)))
            shots = [(2000, 'tier%d-654min' % tier), (4000, 'tier%d-999h59m' % tier),
                     (6500, 'tier%d-long' % tier)]
            log, images = self.launch('2500:RIGHT;4500:RIGHT;7000:QUIT', shots)
            if artifacts:
                (artifacts / ('tier%d.log' % tier)).write_text(log)
            for image in images.values():
                self.assertIsNotNone(image.getbbox())
            # Derived from the actual board/tier font geometry and the main Chrome inset.
            # x3: 528x792; X4 Pro: 480x800. Each value is exclusive last-row ink/cover bottom.
            x3 = images['tier%d-654min' % tier].width == 528
            left, bottom, line_height = ((344, 575, 33), (276, 656, 38), (271, 641, 38))[tier] if x3 else (
                (300, 460, 33), (228, 632, 38), (196, 616, 43))[tier]
            for name, image in images.items():
                row = ImageChops.invert(image.crop((left, bottom - line_height, image.width - 24, bottom + 3)))
                ink = row.getbbox()
                self.assertIsNotNone(ink, 'missing sixth stat row: ' + name)
                last_ink_bottom = bottom - line_height + ink[3]
                self.assertLessEqual(abs(last_ink_bottom - bottom), 1,
                                     'sixth stat row ink/cover bottom differs: ' + name)
            self.assertNotIn('Card stat cut', log)
            self.assertIn('Card stats rows=3f', log, 'six record-backed rows missing')


if __name__ == '__main__':
    unittest.main(defaultTest='RecentDurationFixture.test_duration_columns_three_tiers')
