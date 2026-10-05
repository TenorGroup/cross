"""Inventory actual production filtered rows using the existing category harness."""
from pathlib import Path
import argparse,json,re,subprocess
parser=argparse.ArgumentParser()
parser.add_argument('--source-root',type=Path,default=Path(__file__).resolve().parents[2])
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args()
repo=args.source_root.resolve()
out=args.output.resolve()
h=(repo/'test/settings_catalog/category/harness.cpp').read_text()
old='std::printf("%s\\\"%d:%d:%s\\\"", i ? "," : "", static_cast<int>(row.nameId), static_cast<int>(row.action), row.key ? row.key : "");'
new='''const int count = row.type == SettingType::TOGGLE ? 2 : row.type == SettingType::ENUM ?
        static_cast<int>(row.enumStringValues.empty() ? row.enumLabels().size() : row.enumStringValues.size()) :
        row.type == SettingType::VALUE && row.valueRange.step ? (row.valueRange.max-row.valueRange.min)/row.valueRange.step+1 : 0;
      std::printf("%s{\\\"name\\\":%d,\\\"action\\\":%d,\\\"key\\\":\\\"%s\\\",\\\"type\\\":%d,\\\"options\\\":%d}",i?",":"",static_cast<int>(row.nameId),static_cast<int>(row.action),row.key?row.key:"",static_cast<int>(row.type),count);'''
assert old in h
h=h.replace(old,new)
h=h.replace('#include "CategoryMethods.inc"','#include "CategoryMethods.inc"')
probe=out/'inventory-harness.cpp';probe.write_text(h)
for profile in ['c3','pro']:
 cmd=json.loads((out/'after'/f'{profile}-command.json').read_text())
 cmd=[str(probe) if x.endswith('/category/harness.cpp') else x for x in cmd]
 cmd[-1]=str(out/f'inventory-{profile}')
 subprocess.run(cmd,check=True)
keys=(repo/'lib/I18n/I18nKeys.h').read_text()
ids=re.findall(r'^\s*(STR_[A-Z0-9_]+)\s*,',keys[keys.index('enum class StrId'):],re.M)
results=[]
for board in ['x3','x4','pro']:
 for rtc in [0,1]:
  cmd=[str(out/f'inventory-{"pro" if board=="pro" else "c3"}'),'--enforce',board,str(rtc),'1','1']
  r=subprocess.run(cmd,capture_output=True,text=True,check=True)
  data=json.loads(r.stdout)
  for tab in data['tabs']:
   for row in tab['rows']:row['name']=ids[row['name']]
  results.append(data)
(out/'actual-inventory.json').write_text(json.dumps(results,ensure_ascii=False,indent=2)+'\n')
for row in results:print(row['board'],'rtc',row['rtc'],'rows', [t['size'] for t in row['tabs']], 'total',row['retained_rows'])
