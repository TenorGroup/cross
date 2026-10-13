"""A trailing file preview keeps its book mark when the next page is below it."""
import json
import unittest

from ugly_common import Card, entered


NAMES = [
    'Bác sĩ an tử - Stephen King',
    'Bảy Thanh Hung Giản (Bộ 4 Tập) - v',
    'Bo Con Ca Gai - Cho Chang In',
    'Co Do - Yasunari Kawabata_optimized',
    'Cổ_Mộ_Mật_Mã_-_Ngữ_Mật_TV',
    'Cuoc chien luan hoi - All you n - ttav',
    'Dân Gian Ngụy Văn Thực Lục - La KN',
    'De Nhat Danh Sach - Hoi Thuyet Thoai',
    'Đi tìm thời gian đã mất - Marcel Proust',
    'Đường về nhà thật xa - Những câu chuyện dài',
    'Giấc mơ về một miền đất khác - Người kể chuyện',
    'Hành trình qua những vùng đất xa - Tuyển tập',
]


class UglyPreviewIcon(unittest.TestCase):
    def test_file_preview_and_next_page_keep_the_icon_column(self):
        card = Card(books=(), stats=False, uiUglyLevel=0, uiTextSize=0, tipsEnabled=0, wakeIntoBook=0)
        self.addCleanup(card.close)
        books = card.sd / 'Epub optimize'
        books.mkdir()
        for name in NAMES:
            (books / (name + '.epub')).write_text('fixture')
        (card.store / 'state.json').write_text(json.dumps({'openEpubPath': '', 'showBootScreen': False}))
        script = '2500:DOWN;4000:DOWN;5500:CONFIRM;'
        script += ';'.join(f'{8500 + i * 700}:RIGHT' for i in range(8))
        script += ';17000:QUIT'
        log, shots = card.run(script, [(7500, 'preview'), (16000, 'next_page')], timeout=90,
                              CROSSPOINT_SIM_NOW='1791700000', CROSSPOINT_SIM_MARK_SEED='7')
        self.assertIn('FileBrowser', entered(log), log[-1500:])
        centers = {'preview': [135, 190, 242, 326, 410, 494, 578, 660],
                   'next_page': [130, 214, 298, 382, 466, 550, 634]}
        for name, ys in centers.items():
            image = shots[name]
            column = []
            for y in ys:
                with self.subTest(page=name, row_y=y):
                    points = [(x, z) for z in range(y - 18, y + 19) for x in range(32, 68)
                              if image.getpixel((x, z)) == 0]
                    self.assertGreaterEqual(len(points), 40, 'visible row lost its book mark')
                    column.append((min(x for x, _ in points) + max(x for x, _ in points)) / 2)
            self.assertLessEqual(max(column) - min(column), 0.5, 'book marks left the column')


if __name__ == '__main__':
    unittest.main()
