#!/usr/bin/env python3
"""Build actual settings production source in an isolated host harness.

No network or PlatformIO builds. All eight capability branches get cold tests
in independent processes. UI screen integration is covered by the sibling suite.
"""
import argparse, hashlib, itertools, json, subprocess
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('--repo',required=True,type=Path)
p.add_argument('--work',required=True,type=Path)
p.add_argument('--cmake',default='cmake')
p.add_argument('--jobs',default='4')
a=p.parse_args(); repo=a.repo.resolve(); out=a.work.resolve(); here=Path(__file__).resolve().parent
out.mkdir(parents=True,exist_ok=True)
activity=(repo/'src/activities/settings/SettingsActivity.cpp').read_text()
start=activity.index('std::string SettingsActivity::settingValueText(')
end=activity.index('\nvoid SettingsActivity::buildScreen',start)
(out/'SettingsValueSlice.cpp').write_text('#include "SettingsList.h"\n'+activity[start:end]+'\n')
def home_slice(home_source, destination):
 text=home_source.read_text()
 start=text.index('  if (activeTabId == Tab::FAVORITES && menucustom::state().pinCount > 0) {')
 end=text.index('  rowItems.reserve(rowLabels.size());',start)
 block=text[start:end]
 destination.write_text('''#include "SettingsList.h"
#include "MenuFavorites.h"
#include "MenuCustomization.h"
namespace menucustom { State& state(){static State data;return data;} }
// File path lookup and NFC are boundary doubles, exercised with an ASCII fixture.
namespace filefavorites {
bool isFileKey(const std::string& key) { return key.rfind("folder/", 0) == 0 || key.rfind("bookid/", 0) == 0; }
std::string pathFor(const std::string&) { return "/books/probe.epub"; }
}
std::string utf8ComposeNfc(const std::string& value){return value;}
void runHomeFavorites(const SdCardFontRegistry& registry,std::vector<std::string>& favoriteKeys,
 std::vector<std::string>& favoriteValues,std::vector<std::string>& rowLabels) {
 enum class Tab { FAVORITES };
 const auto activeTabId=Tab::FAVORITES;
 struct FontSystemBoundary { const SdCardFontRegistry& ref;const SdCardFontRegistry& registry()const{return ref;} };
 const FontSystemBoundary sdFontSystem{registry};
'''+block+'}\n')
 return block

home_block=home_slice(repo/'src/activities/home/HomeActivity.cpp',out/'HomeFavoritesSlice.cpp')
incs=[here/'stubs',repo/'test/host_stubs',repo/'src',repo/'freeink-sdk/libs/hardware/BoardConfig/include']
incs += sorted((repo/'.pio/libdeps/gh_release').glob('*/src'))
# SDK test stubs are independent hardware doubles, never production headers.
# In particular BleKeyboardHost/tests/stubs/BoardConfig.h lacks real board APIs.
excluded_parts={'test','tests','stubs','pro_stubs','lucide','.git'}
for base in [repo/'lib',repo/'freeink-sdk',repo/'src']:
 incs += [path for path in sorted(base.rglob('*')) if path.is_dir()
          and not excluded_parts.intersection(path.relative_to(base).parts)]
incs=list(dict.fromkeys(incs))
# A trimmed simulator header can omit capability macros altogether. Include the
# real board types, then remove both optional macros for this compile variant.
legacy_header=out/'LegacyCapabilities.h'
legacy_header.write_text('#include <BoardConfig.h>\n#undef FREEINK_CAP_FRONTLIGHT\n#undef FREEINK_CAP_WARMLIGHT\n')
sources=[here/'SettingsRamProbe.cpp',here/'LinkStubs.cpp',out/'SettingsValueSlice.cpp',out/'HomeFavoritesSlice.cpp',repo/'src/MenuFavorites.cpp',repo/'src/CrossPointSettings.cpp',repo/'src/ReaderFontSizes.cpp',repo/'lib/I18n/I18n.cpp',repo/'lib/I18n/I18nStrings.cpp']
manifest={'source_sha256':{str(p.relative_to(repo)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [repo/'src/SettingsList.h',repo/'src/MenuFavorites.cpp',repo/'src/CrossPointSettings.cpp',repo/'src/activities/settings/SettingsActivity.cpp',repo/'src/activities/home/HomeActivity.cpp']}}
manifest['excerpts_sha256']={'HomeActivity_favorites_block':hashlib.sha256(home_block.encode()).hexdigest(),'settingValueText':hashlib.sha256(activity[start:end].encode()).hexdigest()}
cm='cmake_minimum_required(VERSION 3.16)\nproject(settings_ram_regression CXX)\nset(CMAKE_CXX_STANDARD 20)\nenable_testing()\n'
variants=[(*caps,False) for caps in itertools.product([0,1],repeat=3)]+[(0,0,0,True)]
for front,warm,touch,legacy in variants:
 target='settings_ram_legacy_capabilities' if legacy else f'settings_ram_f{front}w{warm}t{touch}'
 cm+='add_executable('+target+'\n'+''.join(f' "{s}"\n' for s in sources)+')\n'
 cm+=f'target_include_directories({target} PRIVATE\n'+''.join(f' "{p}"\n' for p in incs)+')\n'
 cm+=f'target_compile_definitions({target} PRIVATE FREEINK_DEVICE_X3=1 FREEINK_DEVICE_X4=1 ENABLE_ARDUINO_FEATURES=0 CROSSPOINT_VERSION="host-test" FREEINK_CAP_FRONTLIGHT={front} FREEINK_CAP_WARMLIGHT={warm} FREEINK_CAP_TOUCH={touch})\n'
 if legacy: cm+=f'target_compile_options({target} PRIVATE -include \"{legacy_header}\")\n'
 cm+=f'target_compile_options({target} PRIVATE -ffunction-sections -fdata-sections)\nif(APPLE)\n target_link_options({target} PRIVATE -Wl,-dead_strip)\nelse()\n target_link_options({target} PRIVATE -Wl,--gc-sections)\nendif()\n'
 for imu in [0,1]:
  cm+=f'add_test(NAME cold_{target}_imu{imu} COMMAND {target} cold {imu})\n'
  if not legacy and (front,warm,touch) in [(0,0,0),(1,1,1)]:
   for mode in ['favorites','dynamic','json','v108','home','home-file']:
    cm+=f'add_test(NAME {mode}_{target}_imu{imu} COMMAND {target} {mode} {imu})\n'
(out/'CMakeLists.txt').write_text(cm)
for name,cmd in [('configure',[a.cmake,'-S',str(out),'-B',str(out/'build')]),('build',[a.cmake,'--build',str(out/'build'),'-j',a.jobs]),('ctest',[str(Path(a.cmake).with_name('ctest')),'--test-dir',str(out/'build'),'--output-on-failure','-V'])]:
 with (out/f'{name}.log').open('w') as log: code=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT).returncode
 manifest[name]={'command':cmd,'returncode':code}
 print(f'{name}: {code}',flush=True)
 (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
 if code:raise SystemExit(code)
