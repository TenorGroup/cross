from pathlib import Path
import argparse,hashlib,json,subprocess
p=Path(__file__).resolve().parent
parser=argparse.ArgumentParser()
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--queue',action='store_true')
parser.add_argument('--source-root',type=Path,default=p.parents[1],help='Production snapshot for helper, headers, I18n, ink and Settings methods')
parser.add_argument('--source',type=Path,help='Optional helper .cpp override for mutation testing within the selected snapshot')
parser.add_argument('--profile',choices=['both','x3','x4pro'],default='both')
args=parser.parse_args();repo=args.source_root.resolve();args.output.mkdir(parents=True,exist_ok=True)
source=args.source or repo/'src/shells/ugly/UglyQuestionSheet.cpp'
ink=(repo/'src/shells/ugly/UglyInk.cpp').read_text()
def extract(text,signature):
 start=text.index(signature);opening=text.index('{',start);i=opening+1;depth=1
 while depth:depth+=(text[i]=='{')-(text[i]=='}');i+=1
 return text[start:i]
nav=extract(ink,'void navRow(').replace('{','{\n  ++r.navBars;',1)
settings=repo/'src/activities/settings/SettingsActivity.cpp'
production=[source,repo/'src/shells/ugly/UglyQuestionSheet.h',repo/'src/shells/ugly/UglyInk.h',repo/'src/shells/ugly/UglyInk.cpp']
production += [repo/'lib/I18n'/name for name in ['I18n.h','I18nKeys.h','I18n.cpp','I18nStrings.h','I18nStrings.cpp']]
if args.queue:production.append(settings)
provenance={'source_root':str(repo),'files':[{'path':str(f),'sha256':hashlib.sha256(f.read_bytes()).hexdigest()} for f in production]}
(args.output/'production-manifest.json').write_text(json.dumps(provenance,indent=2)+'\n')
failed=0
for profile in (['x3','x4pro'] if args.profile=='both' else [args.profile]):
 out=args.output/profile;out.mkdir(parents=True,exist_ok=True)
 (out/'NavRow.inc').write_text(nav)
 if args.queue:
  text=settings.read_text()
  (out/'QueueMethods.inc').write_text(extract(text,'void SettingsActivity::queueForm(')+'\n'+extract(text,'bool SettingsActivity::handleCustomInput()')+'\n')
 cmd=['c++','-std=c++20','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-DFREEINK_DEVICE_X4PRO='+str(int(profile=='x4pro')),'-I'+str(repo/'lib/I18n'),'-I'+str(p/'stubs'),'-I'+str(out),'-I'+str(repo/'src'),'-I'+str(repo/'src/shells/ugly'),str(p/('queue.cpp' if args.queue else 'component.cpp')),str(source),str(repo/'lib/I18n/I18n.cpp'),str(repo/'lib/I18n/I18nStrings.cpp'),'-o',str(out/'check')]
 (out/'command.json').write_text(json.dumps(cmd,indent=2)+'\n')
 run=subprocess.run(cmd,capture_output=True,text=True);(out/'build.log').write_text(run.stdout+run.stderr)
 if run.returncode:
  print(profile,run.stderr);failed=1;continue
 run=subprocess.run([str(out/'check')],capture_output=True,text=True);(out/'result.log').write_text(run.stdout+run.stderr)
 print(profile,run.stdout+run.stderr);failed|=bool(run.returncode)
raise SystemExit(failed)
