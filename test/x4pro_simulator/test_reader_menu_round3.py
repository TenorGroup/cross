"""Run the round3 X4 reader menu journey through the real renderer at all 3 UI tiers."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time
import zipfile

from PIL import Image, ImageChops

REPO=Path(__file__).resolve().parents[2]
PROGRAM=Path(os.environ.get('X4PRO_PROGRAM',REPO/'.pio/build/simulator_x4pro/program'))
SCRIPT=('1500:RIGHT;2500:TAP:240,400;3500:TAP:274,754;4300:TAP:240,433;'
        '5100:TAP:240,495;6100:TAP:46,754;6900:TAP:240,557;'
        '7700:SWIPE:240,512,415,512,900;9500:TAP:46,754;10300:TAP:290,495;'
        '11300:TAP:360,495;12100:TAP:94,676;12900:TAP:94,676;13700:TAP:382,484;'
        '14500:TAP:238,676;15300:TAP:382,676;16300:TAP:46,754;'
        '17300:TAP:240,400;18100:BACK;19100:TAP:240,400;19900:SWIPE:4,400,200,400,120;'
        '20900:TAP:240,400;21700:SWIPE:240,798,240,430,120;22900:QUIT')
SHOTS={2300:'reading',2900:'toolbar',3900:'rows',4700:'fonts',5600:'chosen-font',6500:'font-back',
       7300:'spacing',8200:'spacing-held',9000:'spacing-released',9900:'spacing-back',10800:'size-step',
       11700:'keypad',14900:'numeric-30',15800:'numeric-done',16800:'closed',18500:'native-back',
       20300:'edge-back',22300:'home'}

def write_book(path):
    body=''.join(f'<p>Paragraph {n:04d}. '+ 'The reader keeps this place while the text menu changes. '*5+'</p>' for n in range(400))
    with zipfile.ZipFile(path,'w') as z:
        z.writestr('mimetype','application/epub+zip')
        z.writestr('META-INF/container.xml','<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        z.writestr('book.opf','<package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>Reader menu journey</dc:title><dc:identifier id="id">round3-reader</dc:identifier><dc:language>en</dc:language></metadata><manifest><item id="c" href="c.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="c"/></spine></package>')
        z.writestr('c.xhtml','<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Reader</title></head><body><h1>Chapter</h1>'+body+'</body></html>')

def reader_menu_case(tier,output):
    folder=output/f'tier-{tier}';folder.mkdir(parents=True,exist_ok=True)
    sd=folder/'sd';store=sd/'.crosspoint';store.mkdir(parents=True,exist_ok=True);(sd/'books').mkdir(exist_ok=True)
    book=sd/'books/journey.epub';write_book(book);original=hashlib.sha256(book.read_bytes()).hexdigest()
    (store/'settings.json').write_text(json.dumps({'textSpacingVersion':3,'paragraphIndentVersion':1,'readerInkWeightVersion':1,'tenorPresetVersion':1,'uiShellSleepMemo':0,'language':'VI','uiTextSize':tier,'wakeIntoBook':1,'fontSize':18,'fontFamily':0,'lineSpacing':0,'dropCapMode':1,'readerMenuStyle':0,'uiShell':0,'sleepTimeout':10,'readerTapTip':0}))
    (store/'state.json').write_text(json.dumps({'showBootScreen':False,'openEpubPath':'/books/journey.epub','lastSleepFromReader':True}))
    (store/'recent.json').write_text(json.dumps({'books':[{'path':'/books/journey.epub','title':'Reader menu journey'}]}))
    env={k:v for k,v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
    env.update(SDL_VIDEODRIVER='dummy',CROSSPOINT_SIM_SD=str(sd),CROSSPOINT_SIM_WAKE_REASON='power',
               CROSSPOINT_SIM_SD_TRACE='1',CROSSPOINT_SIM_INPUT_SCRIPT=SCRIPT,
               CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{ms}:{folder}/{name}.bmp' for ms,name in SHOTS.items()))
    started=time.monotonic()
    p=subprocess.run([str(PROGRAM)],cwd=REPO,env=env,capture_output=True,text=True,timeout=35)
    log=p.stdout+p.stderr;(folder/'run.log').write_text(log)
    assert p.returncode==0,log[-4000:]
    assert 'Entering activity: EpubReader' in log,log[-4000:]
    assert not re.search(r'Guru Meditation|assert failed|Failed to load page',log),log[-4000:]
    shots={}
    for name in SHOTS.values():
        shots[name]=Image.open(folder/f'{name}.bmp').convert('L');shots[name].save(folder/f'{name}.png')
    assert all(im.size==(480,800) for im in shots.values())
    settings=json.loads((store/'settings.json').read_text())
    assert settings['fontFamily']==1,settings
    assert settings['lineSpacing']==4,settings
    assert settings['fontSize']==18,settings
    assert hashlib.sha256(book.read_bytes()).hexdigest()==original
    def ink(name,box):
        pixels=shots[name].crop(box).getdata();return sum(p<128 for p in pixels)/max(1,len(pixels))
    assert ink('rows',(84,724,464,784))>0.01,'reader tools missing'
    assert ink('keypad',(84,724,464,784))<0.003,'keypad left tool icons visible'
    assert ImageChops.difference(shots['rows'],shots['fonts']).getbbox(),'font row did not open'
    assert ImageChops.difference(shots['spacing-held'],shots['spacing']).getbbox(),'held draft knob stayed fixed'
    assert ImageChops.difference(shots['numeric-30'],shots['keypad']).getbbox(),'numeric keys did not change the draft'
    for name in ('keypad','numeric-30'):
        hint=shots[name].crop((136,402,448,454)).point(lambda p:255 if p<128 else 0).getbbox()
        assert hint and hint[3]-hint[1]>=10 and hint[3]<49,(name,hint,'caption clipped by key grid')
    assert ink('numeric-done',(84,724,464,784))>0.01,'Done did not return to rows'
    assert 'Entering activity: Home' in log,'bottom-edge Home did not exit reader'
    writes=re.findall(r'\[SDW\] write (/[^\n]*settings[^\n]*)',log)
    held_refreshes=sum(7700<=int(m.group(1))<8600 for m in re.finditer(r'^\[(\d+)\].*\[GFX\].*displayBuffer',log,re.M))
    assert held_refreshes<=3,held_refreshes
    assert len(writes)==2,writes
    result={'runtime_ms':round((time.monotonic()-started)*1000),'stationary_hold_refreshes':held_refreshes,'tier':tier,'language':'VI','fixture_families':2,'paragraphs':400,'script':SCRIPT,
            'settings':settings,'settings_write_paths':writes,'book_sha256':original,
            'shots':{name:hashlib.sha256((folder/f'{name}.png').read_bytes()).hexdigest() for name in SHOTS.values()}}
    (folder/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(f'GREEN X4 real renderer tier={tier}: font/spacing/size/Back/Home, {len(SHOTS)} screenshots',flush=True)
    return result

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path);parser.add_argument('--workers',type=int,default=2)
    args=parser.parse_args()
    if args.output:
        args.output.mkdir(parents=True,exist_ok=True)
        with ThreadPoolExecutor(max_workers=min(2,max(1,args.workers))) as pool:results=list(pool.map(lambda t:reader_menu_case(t,args.output),range(3)))
        (args.output/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    else:
        with tempfile.TemporaryDirectory(prefix='x4-reader-menu-') as folder:
            with ThreadPoolExecutor(max_workers=min(2,max(1,args.workers))) as pool:list(pool.map(lambda t:reader_menu_case(t,Path(folder)),range(3)))
    print('GREEN: 3 X4 reader menu round3 real journeys')

if __name__=='__main__':main()
