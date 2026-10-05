from pathlib import Path
import argparse,hashlib,json,sys
repo=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser()
parser.add_argument('--program',type=Path,required=True)
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args()
sys.path.insert(0,str(repo/'test/reading_stats_simulator'))
import ugly_common as u
out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
u.PROGRAM=args.program.resolve()
sha=hashlib.sha256(u.PROGRAM.read_bytes()).hexdigest()
assert sha=='807439c0b23f470e2ee0731082e9292afa20a7baeb5a0f5fe39efb27010967de'
u.ARTIFACTS=str(out)
for level in (0,1):
 card=u.Card(uiUglyLevel=level,uiTextSize=0,uiTheme=4)
 try:
  name=f'x3-ugly{level}-display-old-route'
  script='1000:UP;2000:RIGHT;3000:CONFIRM;5000:QUIT'
  log,images=card.run(script,shots=[(4200,name)])
  (out/(name+'.log')).write_text(log)
  (out/(name+'-settings.json')).write_text(json.dumps(card.settings(),ensure_ascii=False,indent=2))
  route=u.entered(log)
  (out/(name+'-route.json')).write_text(json.dumps({'binary_sha256':sha,'route':route,'script':script},indent=2))
  assert route[-1]=='Settings',route
  assert name in images
  print(name,route)
 finally:card.close()
assert hashlib.sha256(u.PROGRAM.read_bytes()).hexdigest()==sha
