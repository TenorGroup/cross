"""Home favorites, tab order, SD recovery and navigation through real simulator input."""
from pathlib import Path
from PIL import Image
import os,json,subprocess
r=Path(__file__).resolve().parents[2]
o=Path(os.environ.get('MENU_TEST_OUTPUT', '/tmp/tenor-menu-tests'));o.mkdir(parents=True, exist_ok=True)
p=Path(os.environ.get('TEST_PROGRAM', str(r/'.pio/build/simulator_x3_uc8279/program')))
def run(name,script,shots=(),state=None,settings=None,reuse=None):
 sd=o/('sd-'+(reuse or name));s=sd/'.crosspoint';s.mkdir(parents=True,exist_ok=True)
 if not reuse: (s/'settings.json').write_text(json.dumps(dict(language='VI',uiTheme=4,sleepScreen=8,**(settings or {}))))
 if state is not None: (s/'menu-customization.json').write_text(json.dumps(state))
 env={k:v for k,v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')};env.update(SDL_VIDEODRIVER='dummy',CROSSPOINT_SIM_SD=str(sd),CROSSPOINT_SIM_INPUT_SCRIPT=script,CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{ms}:{o}/{name}-{label}.bmp' for ms,label in shots))
 a=subprocess.run([str(p)],env=env,cwd=r,capture_output=True,text=True,timeout=45);(o/(name+'.log')).write_text(a.stdout+a.stderr);assert a.returncode==0,a.stderr
 for _,label in shots:
  path=o/f'{name}-{label}.bmp'
  if path.exists(): Image.open(path).save(path.with_suffix('.png'))
 return sd
def state(pins=(),**tabs):
 d=dict(home=[0,1,4,2,3],settings=list(range(7)),reader=list(range(4)),text=list(range(4)));d.update(tabs)
 return dict(version=1,tabs=d,pins=list(pins))

import sys
t=sys.modules[__name__]
import json,concurrent.futures,time
from pathlib import Path
from PIL import Image,ImageChops

def saved(sd,name):return json.loads((sd/'.crosspoint'/name).read_text())
def state(pins=(),**overrides):
 tabs=dict(home=[0,1,4,2,3],settings=list(range(7)),reader=list(range(4)),text=list(range(4)));tabs.update(overrides)
 return dict(version=1,tabs=tabs,pins=list(pins))
def same(a,b,box):
 assert ImageChops.difference(Image.open(a).crop(box).convert('RGB'),Image.open(b).crop(box).convert('RGB')).getbbox() is None,(a,b)
def pin():
 sd=t.run('pin-e2e','1000:UP;1500:RIGHT;2000:CONFIRM;2800:CONFIRM:900;4150:BACK;4700:UP;5100:UP;5750:CONFIRM;7100:QUIT',[(6500,'popup')])
 assert saved(sd,'menu-customization.json')['pins']==['settings/sleepScreen']
 assert saved(sd,'settings.json')['sleepScreen']==8
 assert 'Entering activity: Settings' in (t.o/'pin-e2e.log').read_text()
 t.run('pin-restart','1000:DOWN;1500:DOWN;2000:CONFIRM;3200:QUIT',[(2700,'popup')],reuse='pin-e2e')
 same(t.o/'pin-e2e-popup.png',t.o/'pin-restart-popup.png',(0,70,528,720))
def order():
 sd=t.run('reorder-e2e','1000:DOWN:1600;3100:QUIT',[(700,'before'),(2800,'after')])
 assert saved(sd,'menu-customization.json')['tabs']['home']==[1,0,4,2,3]
 same(t.o/'reorder-e2e-before.png',t.o/'reorder-e2e-after.png',(0,155,528,710))
 t.run('reorder-restart','1500:QUIT',[(900,'home')],reuse='reorder-e2e')
 same(t.o/'reorder-e2e-after.png',t.o/'reorder-restart-home.png',(0,70,528,710))
def chord():
 sd=t.run('chord','1000:UP:1700;1200:DOWN:1800;3500:QUIT',[(700,'before'),(3200,'after')])
 assert not (sd/'.crosspoint/menu-customization.json').exists()
 same(t.o/'chord-before.png',t.o/'chord-after.png',(0,70,528,710))
def unpin():
 sd=t.run('unpin','1000:DOWN;1500:DOWN;2100:CONFIRM:900;3550:CONFIRM:900;4900:QUIT',[(4600,'empty')],state(['settings/sleepScreen']))
 assert saved(sd,'menu-customization.json')['pins']==[]
 t.run('unpin-restart','1000:DOWN;1500:DOWN;2300:QUIT',[(2000,'empty')],reuse='unpin')
 same(t.o/'unpin-empty.png',t.o/'unpin-restart-empty.png',(0,70,528,710))
def pinorder():
 sd=t.run('pin-order','1000:DOWN;1500:DOWN;2100:RIGHT:1700;4400:QUIT',[(4000,'after')],state(['settings/sleepScreen','text/fontSize','text/readerInkWeight']))
 assert saved(sd,'menu-customization.json')['pins']==['text/fontSize','settings/sleepScreen','text/readerInkWeight']
def route(name,key,expected,settings=None,press='',shots=()):
 sd=t.run(name,'1000:DOWN;1500:DOWN;2100:CONFIRM;'+press+'4300:QUIT',shots or [(3300,'target')],state([key]),settings)
 assert expected in (t.o/(name+'.log')).read_text(), name
 return sd
def status():
 sd=route('status','status/statusBarClock','Entering activity: StatusBarSettings',{'statusBarClock':1})
 assert saved(sd,'settings.json')['statusBarClock']==2

def clock():
 sd=route('clock','clock/clockFormat','Entering activity: DongHoSettings',{'clockFormat':0})
 assert saved(sd,'settings.json')['clockFormat']==1

def kosync():
 route('kosync','kosync/koServerUrl','Entering activity: KeyboardEntry')
def font():
 sd=route('font','text/fontSize','Entering activity: TextSettings',{'fontSize':18})
 assert saved(sd,'settings.json')['fontSize']==18

def text():
 sd=route('text','text/readerInkWeight','Entering activity: TextSettings',{'readerInkWeight':0})
 assert saved(sd,'settings.json')['readerInkWeight']==0  # no installed weight pack; original guard must remain

def opds():
 route('opds','opds/opdsDownloadFolder','Entering activity:')
 assert 'OpdsServerList' in (t.o/'opds.log').read_text()
def groups():
 sd=t.run('groups','1000:UP;1500:RIGHT;2000:CONFIRM;2700:DOWN:1300;4500:BACK;5400:QUIT',[(4250,'display'),(5000,'home')])
 x=saved(sd,'menu-customization.json');assert x['tabs']['settings']==[1,0,2,3,4,5,6];assert x['tabs']['home']==[0,1,4,2,3]

def homeback():
 route('homeback','status/statusBarClock','Entering activity: StatusBarSettings',press='2700:BACK:1200;',shots=[(4100,'home')])
 log=(t.o/'homeback.log').read_text();assert log.count('Entering activity: Home')>=2

def recover():
 sd=t.o/'sd-recover';d=sd/'.crosspoint';d.mkdir(parents=True,exist_ok=True)
 (d/'menu-customization.bak').write_text(json.dumps(state(['text/fontSize'])))
 (d/'menu-customization.json').write_text('{broken')
 t.run('recover','1000:DOWN;1500:DOWN;2100:CONFIRM;3100:QUIT',[(2750,'size')])
 assert 'Entering activity: TextSettings' in (t.o/'recover.log').read_text()
def normalize():
 s=state(['text/fontSize','text/fontSize','future/missing'],home=[4,4,255,0],settings=[6,6,123])
 sd=t.run('normalize','1000:DOWN:1200;2800:QUIT',[(2500,'order')],s)
 x=saved(sd,'menu-customization.json');assert x['pins']==['text/fontSize','future/missing'];assert x['tabs']['home']==[4,1,0,2,3];assert x['tabs']['settings']==[6,0,1,2,3,4,5]
def writefail():
 sd=t.o/'sd-writefail';d=sd/'.crosspoint/menu-customization.tmp';d.mkdir(parents=True,exist_ok=True);(d/'block').write_text('test')
 sd=t.run('writefail','1000:UP;1500:RIGHT;2000:CONFIRM;2700:CONFIRM:900;4300:QUIT',[(4000,'failed')])
 assert not (sd/'.crosspoint/menu-customization.json').exists()
 assert saved(sd,'settings.json')['sleepScreen']==8

jobs=[pin,order,chord,unpin,pinorder,status,clock,kosync,font,text,opds,groups,homeback,recover,normalize,writefail]
if __name__=='__main__':
 t.o=t.o/('run-'+str(time.time_ns()));t.o.mkdir()
 print(t.o,flush=True)
 with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
  fs={pool.submit(f):f.__name__ for f in jobs}
  failed=[]
  for f in concurrent.futures.as_completed(fs):
   try:f.result();print('PASS',fs[f],flush=True)
   except Exception as e:failed.append(fs[f]);print('FAIL',fs[f],repr(e),flush=True)
 assert not failed,failed
