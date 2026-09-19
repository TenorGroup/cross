import argparse,pathlib,subprocess,sys,tempfile
p=pathlib.Path(__file__).resolve().parent
repo=p.parents[1]
parser=argparse.ArgumentParser()
parser.add_argument('--output')
args=parser.parse_args()
if args.output:
 out=pathlib.Path(args.output);out.mkdir(parents=True,exist_ok=True)
else:
 temporary=tempfile.TemporaryDirectory(prefix='tenor-storage-recovery-');out=pathlib.Path(temporary.name)
# Use source methods verbatim while replacing hardware boundaries with fault injection.
def function(path,signature):
 t=path.read_text();start=t.index(signature);b=t.index('{',start);depth=1;i=b+1
 while depth:
  depth+=(t[i]=='{')-(t[i]=='}');i+=1
 return t[start:i]+'\n'
baseline=False
base=p
sdk=base/'SDCardManager.cpp' if baseline else repo/'freeink-sdk/libs/hardware/SDCardManager/src/SDCardManager.cpp'
http=base/'HttpDownloader.cpp' if baseline else repo/'src/network/HttpDownloader.cpp'
persist=base/'PersistableStore.cpp' if baseline else repo/'lib/Serialization/PersistableStore.cpp'
source='#include "harness_prefix.h"\n'+function(sdk,'String SDCardManager::readFile(')+function(sdk,'bool SDCardManager::writeFile(')+persist.read_text()+function(http,'HttpDownloader::DownloadError HttpDownloader::downloadToFile(')+(p/'test_cases.cpp').read_text()
(out/'generated.cpp').write_text(source)
incs=[p,*( [base] if baseline else []),p/'stubs',repo/'src/network',repo/'lib/Serialization',repo/'.pio/libdeps/gh_release/ArduinoJson/src',repo/'freeink-sdk/libs/hardware/SDCardManager/include']
subprocess.run(['c++','-std=c++17','-DARDUINOJSON_ENABLE_ARDUINO_STRING=1','-DARDUINOJSON_ENABLE_ARDUINO_STREAM=0','-DARDUINOJSON_ENABLE_ARDUINO_PRINT=0',*sum((['-I',str(x)] for x in incs),[]),str(out/'generated.cpp'),'-o',str(out/'test')],check=True)
r=subprocess.run([str(out/'test')],capture_output=True,text=True);print(r.stdout);(out/'green.log').write_text(r.stdout);sys.exit(r.returncode)
