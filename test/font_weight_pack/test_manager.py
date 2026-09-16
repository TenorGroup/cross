"""Compile the production manager with bounded I/O and renderer substitutes."""
from pathlib import Path
import os,subprocess,tempfile,unittest
REPO=Path(__file__).resolve().parents[2]
class ManagerTest(unittest.TestCase):
 def test_variant_identity_fallback_and_ui_reuse(self):
  with tempfile.TemporaryDirectory() as td:
   p=Path(td)
   (p/'Logging.h').write_text('#pragma once\n#define LOG_DBG(...)\n#define LOG_ERR(...)\n')
   (p/'EpdFontFamily.h').write_text('#pragma once\nstruct EpdFontFamily { EpdFontFamily(const int*,const int*,const int*,const int*){} };\n')
   (p/'SdCardFont.h').write_text('''#pragma once
#include <fstream>
#include <cstdint>
struct SdCardFont {
 bool load(const char* path) { std::ifstream in(path); char c=0; in.get(c); return c=='G'; }
 uint32_t contentHash() const { return 12345; }
 int styleCount() const { return 1; }
 const int* getEpdFont(int) const { static int value=1; return &value; }
};
''')
   (p/'GfxRenderer.h').write_text('''#pragma once
#include <map>
#include <EpdFontFamily.h>
class SdCardFont;
struct GfxRenderer {
 std::map<int,EpdFontFamily> fonts;
 const auto& getFontMap() const { return fonts; }
 void registerSdCardFont(int,SdCardFont*){}
 void insertFont(int id,EpdFontFamily font) { fonts.emplace(id,font); }
 void clearFallbackFonts(){}
 void clearSdCardFonts(){}
 void removeFont(int id){fonts.erase(id);}
};
''')
   registry=(REPO/'lib/EpdFont/SdCardFontRegistry.cpp').read_text()
   helpers=registry[registry.index('std::string SdCardFontFileInfo::weightPath'):registry.index('// --- SdCardFontRegistry ---')]
   (p/'helpers.cpp').write_text('#include <SdCardFontRegistry.h>\n#include <algorithm>\n'+helpers)
   (p/'test.cpp').write_text('''#include <SdCardFontManager.h>
#include <SdCardFontRegistry.h>
#include <GfxRenderer.h>
#include <filesystem>
#include <fstream>
#include <cassert>
int main(int argc,char**argv) {
 std::string root=argv[1];
 std::filesystem::create_directories(root+"/weight-1");
 std::filesystem::create_directories(root+"/weight-2");
 SdCardFontFileInfo info{root+"/Example_14.cpfont",14,0,7};
 std::ofstream(info.path)<<"Good";
 std::ofstream(info.weightPath(1))<<"Good";
 std::ofstream(info.weightPath(2))<<"Good";
 SdCardFontFamilyInfo family{"Example",{info}};
 GfxRenderer renderer; SdCardFontManager manager;
 int ids[3];
 for (int w=0; w<3; ++w) {
  assert(manager.loadFamily(family,renderer,15,w));
  assert(manager.currentPointSize()==14 && manager.currentWeight()==w);
  ids[w]=manager.getFontId("Example");
  assert(renderer.fonts.size()==1);
 }
 assert(ids[0]!=ids[1] && ids[0]!=ids[2] && ids[1]!=ids[2]);
 int ui=manager.loadFamilyExtraSize(family,renderer,14);
 assert(ui==ids[0] && ui!=ids[2] && renderer.fonts.size()==2);
 assert(manager.loadFamilyExtraSize(family,renderer,14)==ui);
 std::ofstream(info.weightPath(2))<<"Broken";
 assert(manager.loadFamily(family,renderer,14,2));
 assert(manager.currentWeight()==0 && manager.currentFamilyName()=="Example");
 std::filesystem::remove(info.weightPath(1));
 assert(manager.loadFamily(family,renderer,14,1));
 assert(manager.currentWeight()==0 && manager.getFontId("Example")==ids[0]);
 manager.unloadAll(renderer); assert(renderer.fonts.empty());
}
''')
   manager_source = REPO/'lib/EpdFont/SdCardFontManager.cpp'
   if os.environ.get('CROSSPOINT_TEST_MUTATE_WEIGHT_ID'):
    mutant = p/'manager-mutant.cpp'
    mutant.write_text(manager_source.read_text().replace('if (weight) {', 'if (false) {', 1))
    manager_source = mutant
   command=[os.environ.get('CXX','c++'),'-std=c++20','-I'+str(p),'-I'+str(REPO/'lib/EpdFont'),str(manager_source),str(p/'helpers.cpp'),str(p/'test.cpp'),'-o',str(p/'test')]
   subprocess.run(command,check=True,capture_output=True,text=True)
   subprocess.run([str(p/'test'),str(p/'fonts')],check=True,capture_output=True,text=True)
if __name__=='__main__':unittest.main()
