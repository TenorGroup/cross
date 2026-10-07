"""X4 Pro reader menu Favorites (founder 06/10): a tool on the reader's bar (the 1st since 07/10) lists what the reader pinned,
actions and text settings alike. A hold on a Text or More row pins it and marks it with a small heart; a tap on a pinned text setting steps it
there. Pins of actions save as numbers as before, a text setting as "text/<key>".

Runs the X4 Pro simulator (pio run -e simulator_x4pro). X4PRO_PROGRAM picks another build.
"""
import json
import tempfile
from pathlib import Path

from PIL import ImageChops

from test_thanh_day import run, ink
from test_menu_chu_14 import TEXT_MENU, ROW_Y

FAVORITES = (131, 754)  # the 1st tool of the reader's bar


def main():
    with tempfile.TemporaryDirectory(prefix='x4pro-fav-') as tmp:
        root = Path(tmp)
        # Hold letter spacing (the Text menu's 4th row), open Favorites, tap its 3rd row (after the never-pinned
        # size and sync): letter spacing steps from Default.
        script = (TEXT_MENU + f';12000:TAP:240,{ROW_Y[3]},900;15000:TAP:{FAVORITES[0]},{FAVORITES[1]}'
                  f';18000:TAP:240,{ROW_Y[2]};21000:TAP:46,754')
        favorites, _ = run(root / 'a', script, [17500, 23500])  # the run ends after its last shot
        saved = json.loads((root / 'a/sd/.crosspoint/settings.json').read_text())
        assert saved.get('readerFavorites') == ['text/fontSize', 14, 'text/letterSpacing'], \
            f'pins saved: {saved.get("readerFavorites")}'
        assert saved.get('letterSpacing') == 1, f'a tap in Favorites did not step letter spacing: {saved.get("letterSpacing")}'
        assert ink(favorites, (40, 540, 300, 580)) > 0.01, 'no 3rd row in Favorites'
        # The held row shows a small heart before its value, inside the frame, and loses it on a 2nd hold;
        # nothing else on the panel changes.
        before, pinned_row, unpinned = run(root / 'c', TEXT_MENU + f';12000:TAP:240,{ROW_Y[3]},900'
                                           f';15000:TAP:240,{ROW_Y[3]},900', [11800, 14600, 17600])
        row = (240, ROW_Y[3] - 30, 448, ROW_Y[3] + 30)
        changed = ImageChops.difference(before.crop((0, 40, 480, 800)), pinned_row.crop((0, 40, 480, 800))).getbbox()
        changed = changed and (changed[0], changed[1] + 40, changed[2], changed[3] + 40)
        assert changed, 'a hold drew no heart on the row'
        assert row[0] <= changed[0] and changed[2] <= row[2] and row[1] <= changed[1] and changed[3] <= row[3], \
            f'the heart is outside its row, before the frame edge: {changed}'
        panel = (0, 360, 480, 720)  # below the status strip, whose clock may turn a minute
        assert not ImageChops.difference(before.crop(panel), unpinned.crop(panel)).getbbox(), 'a 2nd hold left the heart'
        # An old file's action pins show as they were: sync alone.
        (old,) = run(root / 'b', TEXT_MENU + f';12000:TAP:{FAVORITES[0]},{FAVORITES[1]}', [14500],
                     settings=dict(readerFavorites=[14], readerFavoriteCount=1, readerFavoritesDaDat=1))
        assert ink(old, (40, 400, 300, 460)) > 0.01 and ink(old, (40, 470, 300, 520)) == 0, 'old pins changed'
    print('GREEN: X4 Pro reader Favorites: a held text row pins, steps there, saves as text/<key>; old pins stay')


if __name__ == '__main__':
    main()
