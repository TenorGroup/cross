"""Reader-menu status and footer regression through production simulator screenshots."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time
import zipfile
from PIL import Image, ImageChops

REPO=Path(__file__).resolve().parents[2]
ANALYZE_ONLY=False

def fixture(sd):
    store=sd/'.crosspoint';store.mkdir(parents=True)
    font=REPO/'test/reading_stats_simulator/fixtures/BeVietnamPro_18.cpfont'
    assert font.exists(),font
    for i in range(8):
        dest=sd/'.fonts'/f'AuditFont{i:02d}'/f'AuditFont{i:02d}_18.cpfont'
        dest.parent.mkdir(parents=True);shutil.copyfile(font,dest)
    book=sd/'journey.epub'
    with zipfile.ZipFile(book,'w') as z:
        z.writestr('mimetype','application/epub+zip')
        z.writestr('META-INF/container.xml','<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        items=''.join(f'<item id="c{i}" href="c{i}.xhtml" media-type="application/xhtml+xml"/>' for i in range(9))
        spine=''.join(f'<itemref idref="c{i}"/>' for i in range(9))
        z.writestr('book.opf','<package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>Menu status audit</dc:title><dc:identifier id="id">menu-status-audit</dc:identifier><dc:language>en</dc:language></metadata><manifest>'+items+'<item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/></manifest><spine toc="ncx">'+spine+'</spine></package>')
        nav=''.join(f'<navPoint id="n{i}" playOrder="{i+1}"><navLabel><text>Chapter {i:02d}</text></navLabel><content src="c{i}.xhtml"/></navPoint>' for i in range(9))
        z.writestr('toc.ncx','<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1"><head><meta name="dtb:uid" content="menu-status-audit"/></head><docTitle><text>Menu status audit</text></docTitle><navMap>'+nav+'</navMap></ncx>')
        for i in range(9):
            body=''.join(f'<p>Stable body {n:02d}. This fixture preserves the page while the menu is open.</p>' for n in range(25))
            z.writestr(f'c{i}.xhtml','<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Chapter</title></head><body><h1>Chapter '+str(i)+'</h1>'+body+'</body></html>')
    (store/'state.json').write_text(json.dumps({'showBootScreen':False,'openEpubPath':'/journey.epub','lastSleepFromReader':True}))
    (store/'recent.json').write_text(json.dumps({'books':[{'path':'/journey.epub','title':'Menu status audit'}]}))
    return hashlib.sha256(book.read_bytes()).hexdigest()

def portrait(image,orientation):
    return image.rotate({0:0,1:90,2:180,3:270}[orientation],expand=True)

def script(profile,orientation):
    actions=['2000:CONFIRM','3000:RIGHT','3500:CONFIRM','4500:CONFIRM']
    # Ten font families force pagination, without changing the selected reader font.
    row_button = 'RIGHT' if profile == 'x3' else 'DOWN'
    actions += [f'{5500+i*350}:{row_button}' for i in range(9)]
    actions += ['9500:BACK','10500:BACK','11500:LEFT','12000:CONFIRM','13000:BACK','14000:RIGHT','14500:RIGHT','15000:CONFIRM']
    shots={2500:'toolbar',4000:'text',5000:'fonts',9000:'fonts-last',12500:'contents',15500:'more'}
    if profile=='x4':
        w,h=(800,480) if orientation%2 else (480,800)
        room=w-100
        def tool(i):
            return f"{(84+room*(i+0.5)/3)/w:.6f},{(h-46)/h:.6f}"
        actions=actions[:13]+['9500:BACK',f'10500:TAP:{tool(0)}',f'11500:TAP:{tool(2)}',f'12500:TAP:{tool(1)}',f'13500:TAP:0.5,{(h-305)/h:.6f}']
        shots={2500:'toolbar',4000:'text',5000:'fonts',9000:'fonts-last',11000:'contents',12000:'more',14000:'point-size'}
    actions += ['20500:QUIT']
    return ';'.join(actions),shots

def run_one(program,profile,output,shell,orientation,tier,hidden):
    folder=output/f'{profile}-shell{shell}-rot{orientation}-tier{tier}'/('hidden' if hidden else 'visible')
    if ANALYZE_ONLY:
        result=json.loads((folder/'result.json').read_text())
        assert result['program_sha256']==hashlib.sha256(program.read_bytes()).hexdigest(),'analysis binary does not match capture'
        return {name:Image.open(folder/(name+'.png')).convert('L') for name in result['shots']},result
    folder.mkdir(parents=True,exist_ok=True);sd=folder/'sd';book_hash=fixture(sd)
    settings={'language':'EN','uiShell':shell,'uiTheme':4,'uiTextSize':tier,'orientation':orientation,'readerMenuStyle':1,'wakeIntoBook':1,'screenMargin':40,'readerStatusBarMode':0,'globalStatusBarMode':1 if hidden else 0,'fontSize':18,'fontFamily':0,'textSpacingVersion':3,'paragraphIndentVersion':1,'readerInkWeightVersion':1,'tenorPresetVersion':1,'uiShellSleepMemo':0,'sleepTimeout':10,'showReaderMenu':1}
    (sd/'.crosspoint/settings.json').write_text(json.dumps(settings))
    commands,shots=script(profile,orientation)
    env={k:v for k,v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
    env.update(SDL_VIDEODRIVER='dummy',CROSSPOINT_SIM_SD=str(sd),CROSSPOINT_SIM_WAKE_REASON='power',CROSSPOINT_SIM_INPUT_SCRIPT=commands,CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{ms}:{folder}/{name}.bmp' for ms,name in shots.items()))
    start=time.monotonic();proc=subprocess.run([str(program)],cwd=REPO,env=env,capture_output=True,text=True,timeout=40)
    log=proc.stdout+proc.stderr;(folder/'run.log').write_text(log)
    assert proc.returncode==0,log[-2000:]
    assert 'Entering activity: EpubReader' in log,log[-2000:]
    assert 'EpubReaderMenu' not in log,'fixture opened classic instead of toolbar'
    images={}
    for name in shots.values():
        image=Image.open(folder/(name+'.bmp')).convert('L');image.save(folder/(name+'.png'));images[name]=image
    assert hashlib.sha256((sd/'journey.epub').read_bytes()).hexdigest()==book_hash
    result={'program':str(program),'program_sha256':hashlib.sha256(program.read_bytes()).hexdigest(),'runtime_ms':round((time.monotonic()-start)*1000),'profile':profile,'shell':shell,'orientation':orientation,'tier':tier,'hidden':hidden,'book_sha256':book_hash,'script':commands,'shots':{name:hashlib.sha256((folder/(name+'.png')).read_bytes()).hexdigest() for name in shots.values()}}
    (folder/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    return images,result

def case(program,profile,output,shell,orientation,tier):
    visible,meta=run_one(program,profile,output,shell,orientation,tier,False)
    hidden,_=run_one(program,profile,output,shell,orientation,tier,True)
    failures=[];measurements={}
    for name,image in visible.items():
        if profile=='x3':
            image=portrait(image,orientation);off=portrait(hidden[name],orientation)
            w,h=image.size
            boxes={'clock':(0,h-42,62,h),'battery':(w-70,h-42,w,h),'front-arrows':(200,h-42,w-70,h)}
        else:
            off=hidden[name];w=image.width
            boxes={'clock':(0,0,100,38),'battery':(w-100,0,w,38)}
        stats={}
        for label,box in boxes.items():
            on=image.crop(box);gone=off.crop(box)
            count=sum(p<128 for p in on.getdata());off_count=sum(p<128 for p in gone.getdata())
            changed=sum(p!=0 for p in ImageChops.difference(on,gone).getdata())
            stats[label]={'visible_ink':count,'hidden_ink':off_count,'changed_pixels':changed,'box':box}
            if count<12 or changed<12:failures.append(f'{name}: {label} absent or unchanged with visibility setting ({stats[label]})')
            if profile=='x4':
                bounds=on.point(lambda p:255 if p<128 else 0).getbbox()
                stats[label]['ink_bounds']=bounds
                if bounds and bounds[1]<8:failures.append(f'{name}: {label} starts above the 8 px status inset')
        measurements[name]=stats
        if profile=='x4':
            w,h=image.size
            bar=(0,h-88,w,h)
            if ImageChops.difference(image.crop(bar),off.crop(bar)).getbbox():
                failures.append(name+': status visibility changed the existing footbar')
            back=sum(p<128 for p in image.crop((16,h-76,76,h-16)).getdata())
            if back<30:failures.append(name+': foot Back missing')
            room=w-100
            tools=sum(p<128 for p in image.crop((84,h-76,w-16,h-16)).getdata())
            if name=='point-size' and tools>12:failures.append('PointSize retained tool icons in footbar')
            if name!='point-size' and tools<100:failures.append(name+': reader tools missing')
    # Compare the row band, excluding clock, title, footbar and current cursor.
    first=visible['fonts'];last=visible['fonts-last']
    w,h=first.size
    if profile=='x4':
        font_box=(32,h-336,w-32,h-276)  # Second row is outside both first and last selection.
    else:
        # Page indicator differs only when the viewport moves, independently of the cursor.
        # Landscape sheets reserve the physical footer on one side, so the rule is narrower than the screen.
        rows=[y for y in range(h-150) if sum(p<128 for p in first.crop((0,y,w,y+1)).getdata())>w*0.75]
        assert rows,'font panel edge missing'
        title_h=(33,38,43)[tier]
        font_box=(w-100,rows[0]+40,w-16,rows[0]+40+title_h)
    if not ImageChops.difference(first.crop(font_box),last.crop(font_box)).getbbox():
        failures.append('Fonts pagination did not move to the last page')
    measurements['fonts-pagination']={'box':font_box}
    result={**meta,'status':'RED' if failures else 'GREEN','measurements':measurements,'failures':failures}
    (output/f'{profile}-shell{shell}-rot{orientation}-tier{tier}'/'assertions.json').write_text(json.dumps(result,indent=2)+'\n')
    print(f"{result['status']} {profile} shell={shell} rot={orientation} tier={tier}: {len(failures)} failures",flush=True)
    return result

def main():
    global ANALYZE_ONLY
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--analyze-only',action='store_true');p.add_argument('--program',type=Path,required=True);p.add_argument('--profile',choices=['x3','x4'],required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--shell',type=int,choices=[0,1]);p.add_argument('--orientation',type=int,choices=range(4));p.add_argument('--tier',type=int,choices=range(3));p.add_argument('--matrix',action='store_true');p.add_argument('--workers',type=int,default=2)
    a=p.parse_args();ANALYZE_ONLY=a.analyze_only;a.program=a.program.resolve();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True)
    params=[(s,o,t) for s in range(2) for o in range(4) for t in range(3) if (a.shell is None or a.shell==s) and (a.orientation is None or a.orientation==o) and (a.tier is None or a.tier==t)] if a.matrix else [(a.shell or 0,a.orientation or 0,a.tier or 0)]
    with ThreadPoolExecutor(max_workers=min(6,max(1,a.workers))) as pool:
        results=list(pool.map(lambda x:case(a.program,a.profile,a.output,*x),params))
    (a.output/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    raise SystemExit(1 if any(r['failures'] for r in results) else 0)
if __name__=='__main__':main()
