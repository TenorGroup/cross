"""Capture real reader simulator journeys; pixel checks and commits are distinct from manual visual acceptance."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time
from PIL import Image, ImageChops, ImageOps

REPO=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('menu_status_fixture',REPO/'test/menu_status_v1053/simulator.py')
base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)

def journey(profile,style,rotation):
    if style=='classic':
        # Edge Down steps tabs; front Right steps rows. Persisted order is Reading, Favorites, Tools, Position.
        actions=['2000:CONFIRM','3200:DOWN','3800:DOWN','4400:DOWN','6000:DOWN','6500:RIGHT',
                 '7000:CONFIRM','8000:RIGHT','9000:CONFIRM','10500:CONFIRM','11500:BACK',
                 '12500:BACK','14000:CONFIRM','15500:BACK','17000:QUIT']
        shots={1500:'reading',2600:'classic-favorites',4900:'classic-reading',7400:'status-popup',
               8400:'status-selected',9800:'status-committed',11000:'status-reopened',
               13200:'closed',14800:'classic-reopened'}
        return ';'.join(actions),shots,{'readerStatusBarMode':1}
    if profile=='x3':
        initial,_=base.script('x3',rotation)
        actions=initial.split(';')[:13]  # Existing toolbar/Text/fonts pagination journey.
        actions+=['9500:CONFIRM','10500:BACK','11500:DOWN','12000:DOWN','12500:CONFIRM',
                  '13500:CONFIRM','14500:CONFIRM','16000:BACK','17000:LEFT','17500:CONFIRM',
                  '18500:BACK','19500:RIGHT','20000:RIGHT','20500:CONFIRM','21500:BACK',
                  '22500:BACK','24000:CONFIRM','25000:BACK','26500:QUIT']
        shots={1500:'reading',2500:'toolbar',4000:'text',5000:'fonts',9000:'fonts-last',
               10000:'font-committed',11000:'font-back',13000:'spacing-choice1',
               14000:'spacing-choice2',15000:'spacing-choice3',18000:'contents',
               21000:'more',23200:'closed',24600:'toolbar-reopened'}
        return ';'.join(actions),shots,{'lineSpacing':3,'sdFontFamilyName_prefix':'AuditFont'}
    w,h=(800,480) if rotation%2 else (480,800)
    def tap(ms,x,y):return f'{ms}:TAP:{int(x)},{int(y)}'
    def tool(ms,i):return tap(ms,84+(w-100)*(i+0.5)/3,h-46)
    cw=(w-56)//3
    keyx=[24+cw//2,24+cw+4+cw//2,24+2*(cw+4)+cw//2]
    actions=['1500:RIGHT',tap(2500,w/2,h/2),tool(3500,1),tap(4300,w/2,h-367),
             tap(5100,w/2,h-305),tap(6100,46,h-46),tap(6900,w/2,h-243),
             f'7700:SWIPE:{w//2},{h-288},{w-65},{h-288},900',tap(9500,46,h-46),
             tap(10300,w-190,h-305),tap(11300,w-120,h-305),tap(12100,keyx[0],h-124),
             tap(12900,keyx[0],h-124),tap(13700,keyx[2],h-316),tap(14500,keyx[1],h-124),
             tap(15300,keyx[2],h-124),tool(17300,0),tool(18300,2),
             tap(19300,46,h-46),tap(20500,w/2,h/2),'21500:BACK',
             tap(22500,w/2,h/2),f'23500:SWIPE:{w//2},{h-2},{w//2},{h//2},120','25000:QUIT']
    shots={2300:'reading',2900:'toolbar',3900:'text',4700:'fonts',5600:'font-committed',
           6500:'font-back',7300:'spacing',8200:'spacing-held',9000:'spacing-released',
           9900:'spacing-back',10800:'size-step',11700:'keypad',14900:'numeric-30',
           15800:'numeric-done',17800:'contents',18800:'more',19800:'closed',
           21000:'toolbar-reopened',21900:'native-back',22900:'toolbar-edge',24300:'home'}
    return ';'.join(actions),shots,{'fontFamily':1,'lineSpacing':4}

def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def settings(profile,style,level,rotation,tier):
    return dict(language='VI',uiShell=1,uiUglyLevel=level,uiTheme=4,uiTextSize=tier,orientation=rotation,
                readerMenuStyle=0 if style=='classic' else 1,wakeIntoBook=1,sleepTimeoutMinutes=31,
                fontSize=18,fontFamily=0,lineSpacing=0,screenMargin=40,tapForReaderMenu=1,
                readerStatusBarMode=0,globalStatusBarMode=0,clockShowHeader=1,statusBarClock=1,clockHasBeenSynced=1,
                tenorPresetVersion=1,textSpacingVersion=3,paragraphIndentVersion=1,readerInkWeightVersion=1,
                uiShellSleepMemo=0,readerFavorites=[3,4])

def capture(program,folder,seed,commands,shots,profile,style,expected):
    folder.mkdir(parents=True,exist_ok=True);sd=folder/'sd';shutil.copytree(seed,sd)
    original=digest(sd/'journey.epub')
    env={k:v for k,v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
    env.update(SDL_VIDEODRIVER='dummy',CROSSPOINT_SIM_SD=str(sd),CROSSPOINT_SIM_WAKE_REASON='power',
               CROSSPOINT_SIM_SD_TRACE='1',CROSSPOINT_SIM_INPUT_SCRIPT=commands,
               CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{ms}:{folder}/{name}.bmp' for ms,name in shots.items()))
    started=time.monotonic();proc=subprocess.run([str(program)],cwd=REPO,env=env,capture_output=True,text=True,timeout=50)
    log=proc.stdout+proc.stderr;(folder/'run.log').write_text(log)
    failures=[]
    if proc.returncode:failures.append(f'process returned {proc.returncode}')
    if 'Entering activity: EpubReader' not in log:failures.append('reader never opened')
    if re.search(r'Guru Meditation|assert failed|Failed to load page',log):failures.append('runtime error marker')
    classic='Entering activity: EpubReaderMenu' in log
    if classic!=(style=='classic'):failures.append('wrong menu route')
    images={}
    for name in shots.values():
        path=folder/(name+'.bmp')
        if not path.exists():failures.append('missing '+name);continue
        im=Image.open(path).convert('L');im.save(folder/(name+'.png'));images[name]=im
    final=json.loads((sd/'.crosspoint/settings.json').read_text())
    for key,value in expected.items():
        if key.endswith('_prefix'):
            if not str(final.get(key[:-7],'')).startswith(value):failures.append('commit missing '+key)
        elif final.get(key)!=value:failures.append(f'commit mismatch {key}: {final.get(key)} != {value}')
    if digest(sd/'journey.epub')!=original:failures.append('book bytes changed')
    pairs=[('status-popup','status-selected')] if style=='classic' else [('text','fonts'),('fonts','font-committed')]
    if profile=='x4' and style=='toolbar':pairs += [('spacing','spacing-held'),('keypad','numeric-30'),('keypad','numeric-done')]
    changes={}
    for first,last in pairs:
        if first in images and last in images:
            changes[first+'->'+last]=ImageChops.difference(images[first],images[last]).getbbox()
            if not changes[first+'->'+last]:failures.append('no visible transition '+first+'->'+last)
    # Existing production Page render/PREVIEW_PAGE timers describe book rendering, not menu CPU drawing.
    result=dict(program=str(program),program_sha256=digest(program),script=commands,fixture_book_sha256=original,
                process_wall_ms=round((time.monotonic()-started)*1000),reader_menu_cpu_render_ms=None,
                timer_note='No menu CPU render timer exists. Whole-process wall time includes scheduled waits and screenshots.',
                settings=final,settings_write_paths=re.findall(r'\[SDW\] write (/[^\n]*settings[^\n]*)',log),
                transitions=changes,shots={name:digest(folder/(name+'.png')) for name in images},failures=failures,
                status='RED' if failures else 'AUTOMATED_PASS_VISUAL_PENDING',
                manual_gates=['title/tab labels readable','Ugly handwriting and pencil shapes, no residual Cross ink',
                              'shared clock/battery/pin and physical hints visible','selected and committed labels match',
                              'page preview remains above sheet','landscape glyphs/controls stay in their bounds'])
    (folder/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    return images,result

def run_case(a,style,level,rotation,tier):
    folder=a.output/f'{a.profile}-{style}-level{level}-rot{rotation}-tier{tier}'
    if folder.exists():raise RuntimeError(f'Use a fresh output directory: {folder}')
    seed=folder/'seed';seed.mkdir(parents=True);base.fixture(seed)
    (seed/'.crosspoint/settings.json').write_text(json.dumps(settings(a.profile,style,level,rotation,tier)))
    (seed/'.crosspoint/menu-customization.json').write_text(json.dumps(dict(version=1,pins=[],tabs=dict(reader=[2,0,3,1]))))
    commands,shots,expected=journey(a.profile,style,rotation)
    before={};old=None
    if a.baseline:before,old=capture(a.baseline,folder/'baseline',seed,commands,shots,a.profile,style,expected)
    after,result=capture(a.program,folder/'candidate',seed,commands,shots,a.profile,style,expected)
    result.update(profile=a.profile,style=style,level=level,rotation=rotation,tier=tier)
    if old:
        result['baseline']=dict(program_sha256=old['program_sha256'],status=old['status'],failures=old['failures'])
        result['baseline_menu_cpu_delta_ms']=None
        result['baseline_changed_frames']={name:ImageChops.difference(before[name],image).getbbox() for name,image in after.items() if name in before}
    thumbs=[]
    for name,image in after.items():
        thumb=ImageOps.contain(image.convert('RGB'),(320,320));cell=Image.new('RGB',(340,350),'white');cell.paste(thumb,((340-thumb.width)//2,20))
        from PIL import ImageDraw
        ImageDraw.Draw(cell).text((8,332),name,fill='black');thumbs.append(cell)
    if thumbs:
        sheet=Image.new('RGB',(340*4,350*((len(thumbs)+3)//4)),'#dddddd')
        for i,cell in enumerate(thumbs):sheet.paste(cell,((i%4)*340,(i//4)*350))
        sheet.save(folder/'contact-sheet.png')
    (folder/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(result['status'],folder.name,len(result['failures']),flush=True)
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--profile',choices=['x3','x4'],required=True)
    p.add_argument('--program',type=Path);p.add_argument('--baseline',type=Path);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--styles',nargs='+',choices=['toolbar','classic']);p.add_argument('--levels',nargs='+',type=int,choices=[0,1],default=[0,1])
    p.add_argument('--rotations',nargs='+',type=int,choices=range(4),default=[0,1]);p.add_argument('--tiers',nargs='+',type=int,choices=range(3),default=[0])
    p.add_argument('--workers',type=int,choices=[1,2],default=1);p.add_argument('--plan-only',action='store_true');a=p.parse_args()
    a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True)
    styles=a.styles or (['toolbar','classic'] if a.profile=='x3' else ['toolbar'])
    if a.profile=='x4' and 'classic' in styles:p.error('X4 uses toolbar regardless of readerMenuStyle; select toolbar')
    cases=[(s,l,r,t) for s in styles for l in a.levels for r in a.rotations for t in a.tiers]
    plan=[dict(profile=a.profile,style=s,level=l,rotation=r,tier=t,script=journey(a.profile,s,r)[0],shots=journey(a.profile,s,r)[1],expected_commits=journey(a.profile,s,r)[2]) for s,l,r,t in cases]
    (a.output/'plan.json').write_text(json.dumps(plan,indent=2)+'\n')
    if a.plan_only:print(f'Prepared {len(cases)} cases; no simulator launched');return
    if not a.program:p.error('--program is required for capture')
    a.program=a.program.resolve();a.baseline=a.baseline.resolve() if a.baseline else None
    with ThreadPoolExecutor(max_workers=a.workers) as pool:results=list(pool.map(lambda c:run_case(a,*c),cases))
    (a.output/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    raise SystemExit(1 if any(r['failures'] for r in results) else 0)
if __name__=='__main__':main()
