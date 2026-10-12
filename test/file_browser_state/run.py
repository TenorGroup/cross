import argparse
from pathlib import Path
import subprocess

here = Path(__file__).resolve().parent
repo = here.parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--no-save', action='store_true')
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
fixture = repo / 'test/storage_recovery'

def function(path, signature):
    text = path.read_text()
    start = text.index(signature)
    brace = text.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end] + '\n'

sdk = repo / 'freeink-sdk/libs/hardware/SDCardManager/src/SDCardManager.cpp'
state = (repo / 'src/activities/home/FileBrowserState.cpp').read_text()
if args.no_save:
    state = state.replace('if (!dirty) return true;', 'if (true) return true;')
# Reuse the storage fault fixture with the actual card writer and JSON recovery methods.
prefix = (fixture / 'harness_prefix.h').read_text().replace('bool initialized=true;', 'int writes=0; bool initialized=true;')
prefix = prefix.replace('if(failOpen)return false;files[p]="";', '++writes;if(failOpen)return false;files[p]="";')
source = prefix + function(sdk, 'String SDCardManager::readFile(') + function(sdk, 'bool SDCardManager::writeFile(')
source += (repo / 'lib/Serialization/PersistableStore.cpp').read_text() + state + (here / 'cases.cpp').read_text()
generated = args.output / 'generated.cpp'
generated.write_text(source)
incs = [fixture / 'stubs', fixture, repo / 'src/network', repo / 'lib/Serialization',
        repo / '.pio/libdeps/gh_release/ArduinoJson/src', repo / 'freeink-sdk/libs/hardware/SDCardManager/include',
        repo / 'src/activities/home']
command = ['c++', '-std=c++20', '-DARDUINOJSON_ENABLE_ARDUINO_STRING=1',
           '-DARDUINOJSON_ENABLE_ARDUINO_STREAM=0', '-DARDUINOJSON_ENABLE_ARDUINO_PRINT=0']
for path in incs:
    command += ['-I', str(path)]
command += [str(generated), '-o', str(args.output / 'test')]
subprocess.run(command, check=True)
subprocess.run([str(args.output / 'test')], check=True)
