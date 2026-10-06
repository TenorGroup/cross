"""Bounded real Text/Status simulator journeys, with observed persisted writes."""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess,sys,threading,time
from PIL import Image
p=argparse.ArgumentParser()
p.add_argument('--program',type=Path,required=True)
p.add_argument('--sha256',required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--profile',choices=['x3','x4pro'],required=True)
p.add_argument('--screen',choices=['text','status'],required=True)
p.add_argument('--scenario',choices=['smoke','roundtrip','margin'],default='smoke')
p.add_argument('--level',type=int,default=0)
p.add_argument('--size',type=int,default=0)
p.add_argument('--locale',default='VI')
p.add_argument('--expect-red',action='store_true')
p.add_argument('--tap',help='Drawn first Size circle x,y for X4 Text roundtrip')
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
assert hashlib.sha256(a.program.read_bytes()).hexdigest()==a.sha256
repo=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(repo/'test/reading_stats_simulator'))
import ugly_common as u
name=f'{a.profile}-{a.screen}-{a.scenario}-u{a.level}-s{a.size}-{a.locale}'
activity='TextSettings' if a.screen=='text' else 'StatusBarSettings'
form='Text' if a.screen=='text' else 'Status'
card=u.Card(language=a.locale,uiUglyLevel=a.level,uiTextSize=a.size,
            sleepTimeoutMinutes=10,clockShowHeader=1,statusBarClock=1,
            statusBarItemsMode=2,statusBarTitle=1,statusBarChapterPageCount=1,
            statusBarBookProgressPercentage=1,fontFamily=0,fontSize=16,screenMargin=5)
settings=card.settings()
for legacy in ('sleepTimeout','clockShowInHeader'):settings.pop(legacy,None)
file=card.store/'settings.json';file.write_text(json.dumps(settings))
if a.profile=='x3':
 events=['1000:UP','2000:RIGHT','2750:RIGHT','3500:RIGHT','4250:CONFIRM']
 child=6000
 if a.screen=='status':
  events += [f'{6000+i*900}:RIGHT' for i in range(7)]
  child=12300
 events += [f'{child}:CONFIRM']
 reopen='CONFIRM';back='BACK'
else:
 events=['2000:TAP:240,670','3500:TAP:80,150','5000:TAP:240,368']
 child=6500;reopen='TAP:150,190'
 if a.screen=='status':
  events += [f'{6500+i*1500}:SWIPE:400,400,80,400,200' for i in range(3)]
  child=11000;reopen='TAP:150,580'
 events += [f'{child}:{reopen}'];back='TAP:239,748'
shots=[(child+1000,name+'-entry')];checkpoints=[(child+1200,'entry')]
expected=None;field=None;commit_ms=None;back_ms=None
if a.scenario!='smoke':
 if a.profile=='x3':
  count=7 if a.scenario=='margin' else 1 if a.screen=='text' else 3
  start=child+1800
  events += [f'{start+i*900}:RIGHT' for i in range(count)]
  # Confirm enters the question, the edge key moves its circle, Confirm saves; Confirm on the saved answer leaves it.
  enter=start+count*900;preview=enter+900;commit_ms=preview+1600
  events += [f'{enter}:CONFIRM',f'{preview}:DOWN',f'{commit_ms}:CONFIRM',f'{commit_ms+900}:CONFIRM']
  shots += [(preview+700,name+'-candidate'),(commit_ms+900,name+'-committed')]
  checkpoints += [(preview+900,'candidate'),(commit_ms+1200,'committed')]
  field,expected=('screenMargin',10) if a.scenario=='margin' else ('fontSize',18) if a.screen=='text' else ('statusBarClock',2)
 else:
  assert a.scenario=='roundtrip','Margin ruler uses the X3 physical navigation case'
  assert a.screen!='text' or a.tap,'Supply the actual drawn Size circle after reviewing entry screenshot'
  commit_ms=child+1800;tap=a.tap if a.screen=='text' else '150,190'
  events += [f'{commit_ms}:TAP:{tap}']
  shots += [(commit_ms+900,name+'-committed')];checkpoints += [(commit_ms+1200,'committed')]
  field,expected=('fontSize',12) if a.screen=='text' else ('statusBarTitle',2)
 back_ms=commit_ms+2000;again=back_ms+1500
 events += [f'{back_ms}:{back}',f'{again}:{reopen}']
 checkpoints += [(back_ms+900,'back')]
 focus_count=7 if a.scenario=='margin' else 1 if a.screen=='text' else 3
 if a.profile=='x3':
  events += [f'{again+1800+i*900}:RIGHT' for i in range(focus_count)]
  finish=again+1800+focus_count*900+900
 else:finish=again+1800
 shots += [(finish-200,name+'-reopened')];checkpoints += [(finish-100,'reopened')]
else:finish=child+1800
script=';'.join(events+[f'{finish+300}:QUIT'])
env={k:v for k,v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
env.update(SDL_VIDEODRIVER='dummy',CROSSPOINT_SIM_SD=str(card.sd),CROSSPOINT_SIM_INPUT_SCRIPT=script,
           CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{ms}:{card.sd/(label+".bmp")}' for ms,label in shots))
observed=[];stages={};last_signature=None
try:
 started=time.monotonic();proc=subprocess.Popen([str(a.program)],cwd=repo,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
 output=[]
 def drain():output.extend(proc.communicate(timeout=90))
 thread=threading.Thread(target=drain);thread.start()
 while thread.is_alive():
  elapsed=int((time.monotonic()-started)*1000)
  assert elapsed<95000,'Simulator timeout'
  try:
   stat=file.stat();signature=(stat.st_ino,stat.st_mtime_ns,stat.st_size)
   payload=json.loads(file.read_text())
   if signature!=last_signature:
    observed.append({'ms':elapsed,'inode':stat.st_ino,'mtime_ns':stat.st_mtime_ns,'sha256':hashlib.sha256(file.read_bytes()).hexdigest(),'settings':payload})
    last_signature=signature
   for ms,label in checkpoints:
    if elapsed>=ms and label not in stages:stages[label]={'ms':elapsed,'settings':payload,'signature':list(signature)}
  except (FileNotFoundError,json.JSONDecodeError):pass
  time.sleep(.01)
 thread.join();log=''.join(output);(a.output/(name+'.log')).write_text(log)
 assert proc.returncode==0,log[-2000:]
 images=[]
 for _,label in shots:
  bmp=card.sd/(label+'.bmp');assert bmp.exists(),label
  Image.open(bmp).convert('1').save(a.output/(label+'.png'));images.append(label+'.png')
 frames=re.findall(rf'{form} form total=(\d+)ms rows=(\d+) sheet=(\d+)/(\d+) question=(\d+) candidate=(\d+) paper=(\d+)',log)
 result={'program':str(a.program),'binary_sha256':a.sha256,'fixture_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
         'script':script,'route':u.entered(log),'frames':frames,'stages':stages,'observed_file_versions':observed,'images':images,
         'write_observation_limit':'10 ms polling records observed final-file versions; exact save counts are host-harness acceptance.'}
 (a.output/(name+'.json')).write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
 assert activity in result['route'],result['route']
 if a.expect_red:
  assert not frames,'Old binary unexpectedly contains the new form'
  print(name,'RED reproduced: real child route reached, new form absent')
 else:
  assert frames,'Missing actual production form frames'
  assert all(int(f[1])==(14 if a.screen=='text' else 4) for f in frames),frames
  if field:
   if a.profile=='x3':assert stages['candidate']['settings'][field]==settings[field],stages['candidate']
   for stage in ('committed','back','reopened'):assert stages[stage]['settings'][field]==expected,(stage,field,stages[stage]['settings'][field])
   assert len([x for x in log.splitlines() if 'Entering activity: '+activity in x])==2,'Child must reopen'
   exit_pos=log.index('Exiting activity: '+activity)
   assert 'Settings form' in log[exit_pos:],log[exit_pos:]
  print(name,'PASS',frames,'observed versions',len(observed))
finally:
 card.close()
assert hashlib.sha256(a.program.read_bytes()).hexdigest()==a.sha256
