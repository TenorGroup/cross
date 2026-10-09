import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile

from PIL import Image, ImageChops

from cai_dat_truoc_tenor import truoc_tenor
from test_menu_thanh_x3_v1053 import CHAPTER, CHAPTERS, CONTAINER, NCX, OPF

ROOT = Path(__file__).resolve().parents[2]
FONT = Path(__file__).with_name('fixtures') / 'BeVietnamPro_18.cpfont'
TABS = ['yeu-thich', 'vi-tri', 'doc', 'cong-cu', 'van-ban']


def run_case(program, output, tier, tips, scenario):
    available = int(subprocess.check_output(['df', '-k', '/'], text=True).splitlines()[1].split()[3])
    if available < 14680064:
        raise RuntimeError('CAU HOI: disk below 14 GiB')
    folder = Path(tempfile.mkdtemp(prefix='toolbar57-'))
    try:
        store = folder / '.crosspoint'
        store.mkdir()
        target = folder / '.fonts/BeVietnamPro/BeVietnamPro_18.cpfont'
        target.parent.mkdir(parents=True)
        shutil.copyfile(FONT, target)
        with zipfile.ZipFile(folder / 'book.epub', 'w') as book:
            book.writestr('mimetype', 'application/epub+zip')
            book.writestr('META-INF/container.xml', CONTAINER)
            book.writestr('OEBPS/content.opf', OPF)
            book.writestr('OEBPS/toc.ncx', NCX)
            for chapter in range(1, CHAPTERS + 1):
                body = ''.join(f'<p>Chapter {chapter} paragraph {row:02d} with plain words for a full page.</p>'
                               for row in range(40))
                book.writestr(f'OEBPS/c{chapter}.xhtml', CHAPTER.format(n=chapter, body=body))
        list_menu = scenario == 'danh-sach'
        settings = truoc_tenor({'language': 'VI', 'readerMenuStyle': 0 if list_menu else 1, 'sdFontFamilyName': 'BeVietnamPro',
                               'fontSize': 18, 'sleepTimeout': 120, 'uiShell': 0, 'uiTheme': 4,
                               'uiTextSize': tier, 'tipsEnabled': int(tips), 'readerFavoritesDaDat': 0 if list_menu else 1,
                               'readerFavoriteCount': 0, 'globalStatusBarMode': 0, 'tenorButtonSymbols': 1,
                               'tenorSideArrows': 1, 'readerTapTip': 0})
        (store / 'settings.json').write_text(json.dumps(settings))
        (store / 'state.json').write_text(json.dumps({'openEpubPath': '/book.epub', 'lastSleepFromReader': False,
                                                     'showBootScreen': False, 'readerActivityLoadCount': 0}))
        (store / 'recent.json').write_text(json.dumps({'books': [{'path': '/book.epub', 'title': 'Keys'}]}))
        keys, names = scenarios()[scenario]
        actions = [(1800, 'CONFIRM'), (4000, 'CONFIRM')]
        shots = [(1400, 'home'), (3500, 'page'), (5600, 'tang2-list-yeu-thich' if list_menu else 'tang1-vi-tri')]
        for index, (key, name) in enumerate(zip(keys, names)):
            at = 6000 + 1600 * index
            actions.append((at, key))
            shots.append((at + 1200, name))
        actions.append((shots[-1][0] + 600, 'QUIT'))
        label = f'{scenario}-co{tier}-meo-{"bat" if tips else "tat"}'
        environment = {key: value for key, value in os.environ.items() if not key.startswith('CROSSPOINT_SIM_')}
        environment.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(folder),
                           CROSSPOINT_SIM_INPUT_SCRIPT=';'.join(f'{at}:{key}' for at, key in actions),
                           CROSSPOINT_SIM_HTTP_PORT=str(26000 + tier * 100 + int(tips) * 20 + list(scenarios()).index(scenario)),
                           CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{at}:{folder}/{name}.bmp' for at, name in shots))
        result = subprocess.run([str(program)], cwd=ROOT, env=environment, capture_output=True, text=True, timeout=90)
        (output / f'{label}.log').write_text(result.stdout + result.stderr)
        assert result.returncode == 0, (label, result.returncode, result.stdout[-2000:])
        images = {}
        captured = []
        for at, name in shots:
            image = Image.open(folder / f'{name}.bmp').convert('L')
            images[name] = image
            png = output / f'{label}-{name}.png'
            image.save(png)
            captured.append({'name': name, 'png': str(png), 'sha256': hashlib.sha256(png.read_bytes()).hexdigest(),
                             'at_ms': at})
        if scenario == 'dong':
            assert different(images['tang2-doc'], images['dong-tiep']), 'front Right must move the row'
            assert same(images['tang2-doc'], images['dong-ve']), 'front Left must return to the row'
            assert same(images['tang1-vi-tri'], images['quay-lai']), 'panel Back must restore layer 1'
        if scenario == 'tang1':
            assert same(images['tang1-vi-tri'], images['vong-vi-tri']), 'all 5 tabs must wrap back to Position'
        if scenario == 'tang2':
            assert same(images['tang1-van-ban'], images['quay-lai-van-ban']), 'Text Settings Back must restore Text focus'
        if scenario == 'dong-menu':
            assert same(images['tang1-vi-tri'], images['mo-lai-menu']), 'menu must reopen without changing focus'
            assert same(images['dong-menu'], images['dong-lan2']), 'close must restore the same page'
        if scenario == 'chuong':
            before = images['tang1-vi-tri'].crop((0, 0, 528, 500))
            assert ImageChops.difference(before, images['chuong-sau'].crop((0, 0, 528, 500))).getbbox(), 'side Down must jump chapter'
            assert ImageChops.difference(before, images['chuong-truoc'].crop((0, 0, 528, 500))).getbbox() is None, 'side Up must return to the chapter'
        if scenario == 'muc-luc':
            assert different(images['tang2-vi-tri'], images['muc-luc']), 'Select must open the chapter list'
            assert ImageChops.difference(images['tang1-vi-tri'].crop((0, 700, 528, 764)),
                                         images['quay-lai-muc-luc'].crop((0, 700, 528, 764))).getbbox() is None, 'chapter list Back must restore layer 1'
        if scenario == 'gia-tri':
            assert different(images['dong-thanh-trang-thai'], images['danh-sach-gia-tri']), 'Select must open a value list'
            assert same(images['danh-sach-gia-tri'], images['canh-down']), 'side Down must wait in a value list'
            assert same(images['danh-sach-gia-tri'], images['canh-up']), 'side Up must wait in a value list'
            assert same(images['dong-thanh-trang-thai'], images['huy-gia-tri']), 'value Back must restore its row'
        if list_menu:
            assert same(images['tang2-list-yeu-thich'], images['tang2-list-vong']), 'list tabs must wrap through all 5 icons'
        return {'scenario': scenario, 'tier': tier, 'tips': tips, 'shots': captured}
    finally:
        shutil.rmtree(folder)


def same(first, second):
    difference = ImageChops.difference(first.crop((0, 0, 528, 768)), second.crop((0, 0, 528, 768)))
    difference.paste(0, (430, 764, 528, 768))
    return difference.getbbox() is None


def different(first, second):
    return not same(first, second)


def scenarios():
    return {
        'tang1': (['LEFT', 'RIGHT', 'RIGHT', 'RIGHT', 'RIGHT', 'RIGHT', 'RIGHT'],
                  ['tang1-yeu-thich', 'lap-vi-tri', 'tang1-doc', 'tang1-cong-cu', 'tang1-van-ban',
                   'vong-yeu-thich', 'vong-vi-tri']),
        'tang2': (['CONFIRM', 'UP', 'DOWN', 'DOWN', 'DOWN', 'BACK', 'RIGHT', 'CONFIRM',
                  'DOWN', 'DOWN', 'DOWN', 'DOWN', 'BACK'],
                  ['tang2-vi-tri', 'tang2-yeu-thich', 'lap-vi-tri', 'tang2-doc', 'tang2-cong-cu',
                   'tang1-cong-cu', 'tang1-van-ban', 'van-ban-bo-cuc', 'van-ban-kieu',
                   'van-ban-yeu-thich', 'van-ban-font', 'van-ban-co-chu', 'quay-lai-van-ban']),
        'dong': (['CONFIRM', 'DOWN', 'RIGHT', 'LEFT', 'BACK', 'LEFT'],
                 ['tang2-vi-tri', 'tang2-doc', 'dong-tiep', 'dong-ve', 'quay-lai-doc', 'quay-lai']),
        'chuong': (['DOWN', 'UP'], ['chuong-sau', 'chuong-truoc']),
        'dong-menu': (['BACK', 'CONFIRM', 'BACK'], ['dong-menu', 'mo-lai-menu', 'dong-lan2']),
        'muc-luc': (['CONFIRM', 'CONFIRM', 'BACK'], ['tang2-vi-tri', 'muc-luc', 'quay-lai-muc-luc']),
        'gia-tri': (['CONFIRM', 'DOWN', 'RIGHT', 'CONFIRM', 'DOWN', 'UP', 'RIGHT', 'BACK'],
                   ['tang2-vi-tri', 'tang2-doc', 'dong-thanh-trang-thai', 'danh-sach-gia-tri',
                    'canh-down', 'canh-up', 'gia-tri-tiep', 'huy-gia-tri']),
        'danh-sach': (['DOWN', 'DOWN', 'DOWN', 'DOWN', 'DOWN'],
                      ['tang2-list-vi-tri', 'tang2-list-doc', 'tang2-list-cong-cu', 'tang2-list-van-ban', 'tang2-list-vong']),
    }


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--program', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--tiers', type=int, nargs='+', default=[0, 1, 2])
    parser.add_argument('--scenarios', nargs='+', default=list(scenarios()))
    parser.add_argument('--workers', type=int, default=3)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    program = args.output / 'program'
    shutil.copyfile(args.program, program)
    program.chmod(0o755)
    jobs = [(tier, tips, scenario) for tier in args.tiers for tips in (False, True) for scenario in args.scenarios]
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as pool:
        results = list(pool.map(lambda job: run_case(program.resolve(), args.output.resolve(), *job), jobs))
    (args.output / 'manifest.json').write_text(json.dumps({'program': str(args.program),
         'program_sha256': hashlib.sha256(program.read_bytes()).hexdigest(), 'results': results}, indent=2))
    print(f'GREEN: {len(results)} simulator journeys, {sum(len(result["shots"]) for result in results)} screenshots')
