"""Classified multi-contact app routing; real SDK/GT911 classification is a separate gate."""
import json
import os
import io
import zipfile
from PIL import Image
from pathlib import Path
import tempfile
from concurrent.futures import ThreadPoolExecutor
from test_thanh_day import run, ink


def save(folder, names, images):
    output = os.environ.get('X4PRO_TEST_SHOTS')
    if output:
        target = Path(output) / folder.name
        target.mkdir(parents=True, exist_ok=True)
        (target / 'simulator.log').write_text((folder / 'simulator.log').read_text())
        for name, im in zip(names, images):
            im.save(target / (name + '.png'))


def persisted(folder):
    return json.loads((folder / 'sd/.crosspoint/settings.json').read_text())


def assert_reader(folder):
    assert 'enter EpubReader ' in (folder / 'simulator.log').read_text(), 'fixture never entered Reader'


def reader_entry(shell, time=3000):
    # Ugly Diary's underlined Read word is at y=480 with these two fixtures.
    return f'{time}:TAP:200,480' if shell else f'{time}:TAP:240,300'


def level_case(folder, shell, orientation, axis):
    landscape = orientation in (1, 3)
    w, h = (800, 480) if landscape else (480, 800)
    # Open a book so the renderer uses its persisted orientation. Coordinates
    # are normalized because synthetic plans are parsed before book entry.
    sx, sy = .5, .6
    ex, ey = (sx, sy - 120 / (h - 1)) if axis == 'brightness' else (sx + 120 / (w - 1), sy)
    script = reader_entry(shell) + f';7000:MULTISWIPE:2,{sx},{sy},{ex},{ey},250'
    before, overlay, expired = run(folder, script, [6600, 7600, 9200], settings=dict(
        uiShell=shell, orientation=orientation, frontlightOn=0, frontlightBrightness=60, frontlightWarmth=50))
    assert_reader(folder)
    save(folder, ('page', axis, 'expired'), (before, overlay, expired))
    assert overlay.size == (w, h), 'gesture lost logical orientation'
    box = (w-64, 32, w, h-90) if axis == 'brightness' else (w//2-155, h-152, w//2+155, h-95)
    assert list(before.crop(box).getdata()) != list(overlay.crop(box).getdata()), 'no level overlay'
    page = (0, 24, w, h-90)
    assert list(before.crop(page).getdata()) == list(expired.crop(page).getdata()), 'overlay remained or page changed'
    config = persisted(folder)
    assert config['uiShell'] == shell and config['orientation'] == orientation, 'settings changed beyond light'
    if axis == 'brightness':
        # From off, the first 5% step brings the kept 60% back, three more add 15% (founder 05/10).
        assert config['frontlightBrightness'] == 75 and config['frontlightOn'] == 1, config
    else:
        assert 69 <= config['frontlightWarmth'] <= 70 and config['frontlightOn'] == 0, config
        assert config['frontlightBrightness'] == 60, 'warmth changed brightness while off'


def bounds_case(folder, shell):
    # Off -> up -> brightness100 -> a fast flick down puts it out keeping 100; horizontal clamps both ends.
    script = ('3000:MULTISWIPE:2,.5,.9,.5,.05,250;'
              '5200:MULTISWIPE:2,.5,.05,.5,.95,250;'
              '7400:MULTISWIPE:2,.1,.5,.95,.5,250;'
              '9600:MULTISWIPE:2,.95,.5,.05,.5,250;'
              '11800:MULTISWIPE:2,.95,.5,.05,.5,250')
    images = run(folder, script, [3600,5800,8000,12400,14000], settings=dict(
        uiShell=shell, frontlightOn=0, frontlightBrightness=60, frontlightWarmth=50))
    save(folder, ('100-bright', '0-bright', '100-warm', '0-warm', 'expired'), images)
    config = persisted(folder)
    assert (config['frontlightBrightness'],config['frontlightOn'],config['frontlightWarmth']) == (100,0,0), config


def unsupported_case(folder, shell):
    images = run(folder, reader_entry(shell) + ';7000:MULTISWIPE:1,.5,.6,.5,.3,250;'
                        '8500:MULTISWIPE:3,.5,.6,.5,.3,250;10000:MULTISWIPE:4,.5,.6,.8,.6,250',
                 [6600,7800,9300,10800], settings=dict(uiShell=shell,frontlightOn=1,frontlightBrightness=60))
    assert_reader(folder)
    save(folder, ('before','1-contact','3-contact','4-contact'), images)
    page=(0,24,480,710)
    for im in images[1:]:
        assert list(images[0].crop(page).getdata()) == list(im.crop(page).getdata()), 'unsupported contact changed page'
    config=persisted(folder)
    assert config['frontlightBrightness']==60, 'unsupported contact changed light'


def bottom_case(folder, shell):
    # Ugly: the desk as a reference, its folder, bottom Home (the desk), its book, the reader menu, bottom Home.
    script = ('1000:TAP:400,754;2000:TAP:240,754;3000:TAP:80,630;5500:SWIPE:240,790,240,620,250;'
              '8000:TAP:240,420;12000:TAP:.5,.97;14500:SWIPE:.5,.99,.5,.75,250') if shell else (
              '3000:TAP:423,754;5500:SWIPE:240,790,240,620,250;'
              '8000:TAP:240,300;12000:TAP:.5,.97;14500:SWIPE:.5,.99,.5,.75,250')
    images = run(folder, script,
                 [2700,4800,7300,13800,16400],settings=dict(uiShell=shell,homeButtonTapAction=9))
    assert_reader(folder)
    save(folder, ('desk' if shell else 'recent','folder' if shell else 'settings','home','reader-menu','home-again'), images)
    # Reading updates the Recent card's metadata/progress; its selected foot tab is stable. The desk's top row
    # (statistics, Recent, Settings) is above the open book that shows the progress.
    box=(0,48,480,288) if shell else (16,724,464,784)
    # Ugly: from the book (the last swipe) Home goes to the diary, so only the first Home is the desk.
    for im in (images[2],) if shell else (images[2],images[4]):
        assert list(images[0].crop(box).getdata()) == list(im.crop(box).getdata()), \
            'bottom swipe did not return to the desk' if shell else 'bottom swipe did not return Recent'
    if shell:
        assert list(images[0].crop(box).getdata()) != list(images[4].crop(box).getdata()), 'bottom swipe from the book stayed on the desk'
    assert persisted(folder)['homeButtonTapAction']==9, 'gesture changed Home key preference'



def panel_case(folder, shell):
    # Gesture then immediate edge Home: onExit must read HAL even if loop was skipped.
    images=run(folder, ('1000:TAP:400,754;2000:TAP:240,754;' if shell else '') + '3000:SWIPE:.5,.01,.5,.25,250;'
                      '6000:MULTISWIPE:2,.5,.7,.5,.55,250;'
                      '6300:SWIPE:.5,.99,.5,.75,250',
               [2700,5400,8400],settings=dict(uiShell=shell,frontlightOn=0,frontlightBrightness=60))
    save(folder,('desk' if shell else 'recent','panel','after-home'),images)
    assert persisted(folder)['frontlightBrightness']==75, 'closing panel restored stale brightness'
    assert persisted(folder)['frontlightOn']==1, 'closing panel restored stale off state'
    # Below the status strip (32 px): its clock changes when a run crosses a minute.
    box=(0,48,480,288) if shell else (0,32,480,180)
    assert list(images[0].crop(box).getdata())==list(images[2].crop(box).getdata()), 'panel bottom swipe stopped below Home'


def single_case(folder,shell):
    # Real one-finger classifier remains a swipe available to the current activity.
    run(folder,'3000:SWIPE:.5,.6,.5,.4,250;5200:SWIPE:.3,.5,.6,.5,250',[2800,6500],
        settings=dict(uiShell=shell,frontlightOn=1,frontlightBrightness=60,frontlightWarmth=50))
    config=persisted(folder)
    assert config['frontlightBrightness']==60 and config['frontlightWarmth']==50, 'one finger changed light'


def aa_off_case(folder,shell):
    images=run(folder,reader_entry(shell) + ';7000:MULTISWIPE:2,.5,.6,.5,.45,250',
               [6600,7600,9200],settings=dict(uiShell=shell,textAntiAliasing=0,frontlightOn=0))
    assert_reader(folder)
    save(folder,('page','overlay','expired'),images)
    box=(416,32,480,710)
    assert list(images[0].crop(box).getdata())!=list(images[1].crop(box).getdata()), 'BW page lost overlay'
    page=(0,24,480,710)
    assert list(images[0].crop(page).getdata())==list(images[2].crop(page).getdata()), 'BW fade changed page'


def image_book(directory):
    path=directory/'test_kerning_ligature.epub'
    with zipfile.ZipFile(path) as z:
        parts={name:z.read(name) for name in z.namelist()}
    opf=parts['book.opf'].decode()
    opf=opf.replace('</manifest>', '<item id="gradient" href="gradient.png" media-type="image/png"/></manifest>')
    parts['book.opf']=opf.encode()
    parts['body.xhtml']=b'<html xmlns="http://www.w3.org/1999/xhtml"><body><p>Gradient fixture</p><img src="gradient.png" width="400" height="420"/></body></html>'
    im=Image.new('L',(400,420))
    im.putdata([x*255//399 for y in range(420) for x in range(400)])
    buf=io.BytesIO(); im.save(buf,format='PNG'); parts['gradient.png']=buf.getvalue()
    with zipfile.ZipFile(path,'w') as z:
        for name,data in parts.items(): z.writestr(name,data)


def image_case(folder,shell):
    images=run(folder,reader_entry(shell) + ';8500:MULTISWIPE:2,.5,.6,.5,.45,250',
               [8000,9100,10900],settings=dict(uiShell=shell,frontlightOn=0),write_books=image_book)
    assert_reader(folder)
    save(folder,('image-page','overlay','expired'),images)
    assert len(set(images[0].getdata()))>2, 'fixture did not exercise image grayscale'
    box=(416,32,480,710)
    assert list(images[0].crop(box).getdata())!=list(images[1].crop(box).getdata()), 'image page lost overlay'
    page=(0,24,480,710)
    assert list(images[0].crop(page).getdata())==list(images[2].crop(page).getdata()), 'image fade changed gray page'


def home_surface_case(folder, shell):
    # Cross: bottom Home from Recent stays on Recent. Ugly: reach the desk as a reference, then the diary, then
    # bottom Home lands back on the desk.
    script = ('1000:TAP:400,754;3000:TAP:240,754;5000:TAP:240,754;'
              '7000:SWIPE:.5,.99,.5,.75,250') if shell else '7000:SWIPE:.5,.99,.5,.75,250'
    images = run(folder, script, [4400 if shell else 2400,6600,8200], settings=dict(uiShell=shell,homeButtonTapAction=9))
    save(folder, ('desk-reference','diary-before','desk-after') if shell else ('recent-reference','recent-before','recent-after'),images)
    box=(0,48,480,720) if shell else (16,724,464,784)
    assert list(images[0].crop(box).getdata()) == list(images[2].crop(box).getdata()), \
        'bottom Home from the diary did not reach the desk' if shell else 'bottom Home from Recent did not stay on Recent'
    assert persisted(folder)['homeButtonTapAction']==9, 'bottom Home changed physical key preference'


def main():
    with tempfile.TemporaryDirectory(prefix='x4pro-gestures-') as tmp:
        tasks=[]
        for shell in (0,1):
            for orientation in (0,1,2,3):
                for axis in ('brightness','warmth'):
                    folder=Path(tmp)/f's{shell}-o{orientation}-{axis}'
                    tasks.append((level_case,(folder,shell,orientation,axis)))
            for fn in (bounds_case,unsupported_case,bottom_case,panel_case,single_case,aa_off_case,image_case,home_surface_case):
                tasks.append((fn,(Path(tmp)/f's{shell}-{fn.__name__}',shell)))
        with ThreadPoolExecutor(max_workers=4) as pool:
            futures=[pool.submit(fn,*args) for fn,args in tasks]
            failures=[]
            for (fn,args),f in zip(tasks,futures):
                try: f.result()
                except Exception as error: failures.append(f'{args[0].name}: {error}')
            assert not failures, '\n'.join(failures)
    print('GREEN: 32 gesture cases, 4 orientations, 2 shells, AA/BW/image/panel/levels/idle/Home/no phantom page')

if __name__ == '__main__': main()
