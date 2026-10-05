from pathlib import Path
import argparse,hashlib,json,shlex,subprocess
p=Path(__file__).resolve().parent
parser=argparse.ArgumentParser()
parser.add_argument('--source-root',type=Path,default=p.parents[1])
parser.add_argument('--dependency-root',type=Path,help='Existing simulator SDK/dependencies; defaults to source-root')
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args();r=args.source_root.resolve();d=(args.dependency_root or r).resolve();args.output.mkdir(parents=True,exist_ok=True)
files=[r/f for f in ['src/shells/ugly/UglySwitch.cpp','src/shells/ugly/UglySwitch.h','src/shells/ugly/UglyScreen.h','src/shells/ugly/UglyInk.h','src/shells/ugly/UglyLogic.h','src/shells/ugly/UglyLevel.h','src/shells/ugly/UglyQuip.h','src/shells/ShellKind.h','src/activities/home/DocThuMuc.h','lib/I18n/I18n.h','lib/I18n/I18nKeys.h','lib/Utf8/Utf8.h','src/shells/Shell.h']]
(args.output/'production-manifest.json').write_text(json.dumps({'source_root':str(r),'dependency_root':str(d),'files':[{'path':str(f),'sha256':hashlib.sha256(f.read_bytes()).hexdigest()} for f in files]},indent=2)+'\n')
deps=d/'.pio/libdeps/simulator_x3'
assert (deps/'simulator/src/Arduino.h').exists(),'Existing simulator SDK required; this check never invokes PIO'
includes=[r/'lib/I18n',r/'src',deps/'simulator/src']
includes += sorted(x for x in (r/'lib').iterdir() if x.is_dir())
if d!=r:includes += [d/'src']+sorted(x for x in (d/'lib').iterdir() if x.is_dir())
includes += sorted(x for x in deps.iterdir() if x.is_dir())
includes += sorted(x/'src' for x in deps.iterdir() if (x/'src').is_dir())
failed=0
for profile in ['x3','x4pro']:
 out=args.output/profile;out.mkdir(exist_ok=True)
 defines=['-DSIMULATOR','-DFREEINK_DEVICE_X4PRO='+str(int(profile=='x4pro')),'-D'+('SIMULATOR_DEVICE_X4_PRO' if profile=='x4pro' else 'SIMULATOR_DEVICE_X3')]
 cmd=['c++','-MMD','-MF',str(out/'headers.d'),'-MT','ugly-switch','-std=c++20','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsyntax-only']+defines+['-I'+str(x) for x in includes]+[str(r/'src/shells/ugly/UglySwitch.cpp')]
 (out/'command.json').write_text(json.dumps(cmd,indent=2)+'\n')
 run=subprocess.run(cmd,capture_output=True,text=True);(out/'build.log').write_text(run.stdout+run.stderr)
 if not run.returncode:
  dependency_text=(out/'headers.d').read_text().replace('\\\n',' ')
  headers=[Path(name) for name in shlex.split(dependency_text.split(':',1)[1])]
  (out/'headers-manifest.json').write_text(json.dumps([{'path':str(f),'sha256':hashlib.sha256(f.read_bytes()).hexdigest()} for f in headers],indent=2)+'\n')
 print(profile,'syntax PASS' if not run.returncode else run.stderr);failed|=bool(run.returncode)
raise SystemExit(failed)
