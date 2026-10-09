from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class NotoSelection(unittest.TestCase):
    def test_production_builtin_branch(self):
        source = Path(os.environ.get('CROSSPOINT_NOTO_SELECTION_SOURCE', ROOT / 'src/SdCardFontSystem.cpp')).read_text()
        begin = source.index("if (wantedFamily[0] == '\\0')", source.index('void SdCardFontSystem::ensureLoadedImpl'))
        opening = source.index('{', begin)
        depth, end = 1, opening + 1
        while depth:
            depth += (source[end] == '{') - (source[end] == '}')
            end += 1
        branch = source[begin:end]
        code = r'''
#include <array>
#include <cassert>
#include <cstdint>
#include <string>
struct CrossPointSettings { enum {NOTOSERIF=0,NOTOSANS=1}; };
struct Settings {
 uint8_t fontFamily=0,fontPointSize=14,readerInkWeight=0;
 char sdFontFamilyName[32]="";
 int getReaderFontId()const{return 1000+fontFamily*100+fontPointSize;}
} SETTINGS;
namespace readerInk {
 uint8_t clamp(uint8_t value){return value<6?value:5;}
 uint8_t physical(uint8_t value){return value?value+1:0;}
}
const uint8_t BUILTIN_READER_POINT_SIZES[]={12,14,16,18};
uint8_t snapToNearestPointSize(const uint8_t*,size_t,uint8_t value){return value;}
void snapFontPointSizeTo(uint8_t value){SETTINGS.fontPointSize=value;}
struct Renderer{};
struct Family{};
struct Manager {
 std::string current;
 uint8_t size=0,weight=0;
 int calls=0,id=0;
 bool matching=true,builtin=false;
 const std::string& currentFamilyName(){return current;}
 uint8_t currentPointSize(){return size;}
 bool isBuiltinRaster(){return builtin;}
 void unloadAll(Renderer&){current.clear();weight=0;builtin=false;}
 bool loadBuiltinFamily(const Family&,Renderer& renderer,uint8_t point,uint8_t requested,int target) {
   ++calls;unloadAll(renderer);
   if(!matching)return false;
   current=SETTINGS.fontFamily==0?"NotoSerif":"NotoSans";
   size=point;weight=requested;id=target;builtin=true;return true;
 }
};
struct System {
 Manager manager_;
 uint8_t loadedInkLevel_=0;
 bool notoPackMissing_=false,present=true;
 Family family;
 const Family* familyNamed(const char*){return present?&family:nullptr;}
 bool walkIfCatalogKept(){return false;}
 void apply(Renderer& renderer,bool registryWasDirty=false) {
  notoPackMissing_=false;
  const char* wantedFamily=SETTINGS.sdFontFamilyName;
  const std::string& currentFamily=manager_.currentFamilyName();
''' + branch + r'''
 }
};
int main(){
 Renderer renderer;System system;
 const int id=SETTINGS.getReaderFontId();
 for(int level : {0,5,1,5,0}){
   SETTINGS.readerInkWeight=level;system.apply(renderer);
   assert(SETTINGS.sdFontFamilyName[0]==0 && SETTINGS.getReaderFontId()==id);
   if(level){assert(system.manager_.id==id && system.manager_.isBuiltinRaster());}
   else {assert(!system.manager_.isBuiltinRaster());}
 }
 SETTINGS.readerInkWeight=5;system.present=false;system.apply(renderer);
 assert(system.notoPackMissing_ && system.manager_.weight==0);
 system.present=true;system.manager_.matching=false;system.apply(renderer);
 assert(system.notoPackMissing_ && system.manager_.weight==0);
 system.manager_.matching=true;system.apply(renderer);
 assert(!system.notoPackMissing_);
 const int calls=system.manager_.calls;system.apply(renderer);
 assert(system.manager_.calls==calls);
 SETTINGS.fontFamily=1;system.apply(renderer);
 assert(system.manager_.current=="NotoSans" && system.manager_.id==SETTINGS.getReaderFontId());
 SETTINGS.fontFamily=2;system.apply(renderer);
 assert(!system.manager_.isBuiltinRaster());
}
'''
        with tempfile.TemporaryDirectory() as directory:
            source_path = Path(directory) / 'selector.cpp'
            binary = Path(directory) / 'selector'
            source_path.write_text(code)
            result = subprocess.run(['c++', '-std=c++17', str(source_path), '-o', str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
