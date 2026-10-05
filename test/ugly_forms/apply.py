"""Production shared settings apply/save methods with injected hardware boundaries."""
from pathlib import Path
import argparse,subprocess,hashlib,json
p=Path(__file__).resolve().parent;repo=p.parents[1]
a=argparse.ArgumentParser();a.add_argument('--output',type=Path,required=True);a.add_argument('--source',type=Path,default=repo/'src/activities/settings/SettingsActivity.cpp');args=a.parse_args();args.output.mkdir(parents=True,exist_ok=True)
s=args.source.read_text()
def method(signature):
 start=s.index(signature);opening=s.index('{',start);i=opening+1;depth=1
 while depth:depth+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[start:i]+'\n'
back=method('if (mappedInput.wasReleased(MappedInputManager::Button::Back))')
block=method('bool SettingsActivity::saveSettings(')+method('bool SettingsActivity::applySettingValue(')+'bool SettingsActivity::handleBack(){\n'+back+'return false;\n}\n'
(args.output/'ApplyMethods.inc').write_text(block)
(args.output/'manifest.json').write_text(json.dumps({'source_sha256':hashlib.sha256(s.encode()).hexdigest(),'extracted_sha256':hashlib.sha256(block.encode()).hexdigest()},indent=2))
subprocess.run(['c++','-std=c++20','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(args.output),str(p/'apply_harness.cpp'),'-o',str(args.output/'check')],check=True)
r=subprocess.run([str(args.output/'check')],capture_output=True,text=True);print(r.stdout);(args.output/'result.log').write_text(r.stdout+r.stderr);raise SystemExit(r.returncode)
