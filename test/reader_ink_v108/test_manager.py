"""Exercise real registry/manager with equal content hashes and failing files."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]


class ManagerTest(unittest.TestCase):
    def test_historical_pack_paths_identity_fallback_and_unweighted_ui(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            (out / 'Logging.h').write_text('#pragma once\n#define LOG_DBG(...)\n#define LOG_ERR(...)\n')
            (out / 'Files.h').write_text('''#pragma once
#include <map>
#include <string>
#include <vector>
inline std::map<std::string, char> files;
inline std::vector<std::string> opens;
inline bool layoutMatches=true;
''')
            (out / 'HalStorage.h').write_text('''#pragma once
#include "Files.h"
struct StorageFake { bool exists(const char* path) { return files.count(path); } };
inline StorageFake Storage;
''')
            (out / 'EpdFontFamily.h').write_text('''#pragma once
struct EpdFontFamily {
 const int* regular;
 EpdFontFamily(const int* regular,const int*,const int*,const int*):regular(regular){}
};
''')
            (out / 'SdCardFont.h').write_text('''#pragma once
#include "Files.h"
#include <cstdint>
struct SdCardFont {
 bool load(const char* path) { opens.push_back(path); return files.count(path) && files.at(path)=='G'; }
 uint32_t contentHash() const { return 12345; }
 bool matchesBuiltinLayout(const EpdFontFamily&) const { return layoutMatches; }
 int styleCount() const { return 4; }
 const int* getEpdFont(int) const { static int value=1; return &value; }
};
''')
            (out / 'GfxRenderer.h').write_text('''#pragma once
#include <map>
#include <EpdFontFamily.h>
class SdCardFont;
struct GfxRenderer {
 std::map<int,EpdFontFamily> fonts;
 int clearSdCardFontsCalls=0;
 int builtinRasterId=0;
 const auto& getFontMap() const { return fonts; }
 void registerSdCardFont(int,SdCardFont*){}
 void registerBuiltinRaster(int id,SdCardFont*){builtinRasterId=id;}
 bool replaceBuiltinFont(int id,const EpdFontFamily& font) {
   auto found=fonts.find(id); if(found==fonts.end())return false; found->second=font;return true;
 }
 void insertFont(int id,EpdFontFamily font) { fonts.emplace(id,font); }
 void clearFallbackFonts(){}
 void clearSdCardFonts(){ ++clearSdCardFontsCalls; builtinRasterId=0; }
 void removeFont(int id){fonts.erase(id);}
};
''')
            registry = (REPO / 'lib/EpdFont/SdCardFontRegistry.cpp').read_text()
            helpers = registry[registry.index('std::string SdCardFontFamilyInfo::dir'):registry.index('// --- SdCardFontRegistry ---')]
            (out / 'registry.cpp').write_text('#include <SdCardFontRegistry.h>\n#include <HalStorage.h>\n#include <algorithm>\n' + helpers)
            (out / 'test.cpp').write_text('''#include <SdCardFontManager.h>
#include <SdCardFontRegistry.h>
#include <GfxRenderer.h>
#include <ReaderInkWeight.h>
#include "Files.h"
#include <cassert>
#include <cstdio>
static SdCardFontFamilyInfo family() {
 SdCardFontFamilyInfo family;
 family.name="Example"; family.stems={"Example"}; family.files={{14,0,0}};
 return family;
}
static void install(const SdCardFontFamilyInfo& family, std::initializer_list<int> weights) {
 files.clear(); opens.clear();
 const auto& file=family.files.front();
 for (const int weight : weights) {
   const std::string path=family.filePath(file,static_cast<uint8_t>(weight));
   files[path]='G';
 }
}
int main() {
 const auto full=family();
 const auto& fullFile=full.files.front();
 for (int weight=0;weight<=6;++weight) {
   const std::string path=full.filePath(fullFile,static_cast<uint8_t>(weight));
   assert(full.filePath(fullFile,static_cast<uint8_t>(weight))==path);
 }
 GfxRenderer renderer; SdCardFontManager manager;
 install(full,{0,1,2,3,4,5,6});
 assert(readerInk::publicMask(full.weights(fullFile))==63);
 int fullIds[6];
 for (int level=0;level<6;++level) {
   const int physical=readerInk::physical(level);
   assert(manager.loadFamily(full,renderer,15,physical));
   assert(manager.currentPointSize()==14 && manager.currentWeight()==physical);
   assert(readerInk::publicFromPhysical(manager.currentWeight())==level);
   fullIds[level]=manager.getFontId("Example");
   assert(opens.back()==full.filePath(fullFile,static_cast<uint8_t>(physical)));
   assert(renderer.fonts.size()==1);
 }
 std::printf("SD_LAYOUT_IDS=%d,%d,%d\\n", fullIds[0], fullIds[5], fullIds[0]);
 std::fflush(stdout);
 for(int index=1;index<6;++index)assert(fullIds[index]==fullIds[0]);

 const auto partial=family();
 install(partial,{0,1,2});
 assert(readerInk::publicMask(partial.weights(partial.files.front()))==3);
 opens.clear(); renderer.clearSdCardFontsCalls=0;
 const int sequence[] = {5, 0, 1, 2, 5};
 const uint8_t expectedWeights[] = {2, 0, 2, 2, 2};
 const char* expectedPaths[] = {
   "/.fonts/Example/weight-2/Example_14.cpfont",
   "/.fonts/Example/Example_14.cpfont",
   "/.fonts/Example/weight-2/Example_14.cpfont",
   "/.fonts/Example/weight-2/Example_14.cpfont",
   "/.fonts/Example/weight-2/Example_14.cpfont",
 };
 int baseId=0; int weight2Id=0;
 for (int step=0; step<5; ++step) {
   assert(manager.loadFamily(partial,renderer,14,readerInk::physical(sequence[step])));
   assert(manager.currentWeight()==expectedWeights[step]);
   assert(opens.size()==static_cast<size_t>(step + 1));
   assert(opens.back()==expectedPaths[step]);
   assert(renderer.clearSdCardFontsCalls==step + 1);
   const int id=manager.getFontId("Example");
   if (sequence[step]==0) baseId=id;
   if (sequence[step]!=0) {
     if (weight2Id==0) weight2Id=id;
     else assert(id==weight2Id);
   }
 }
 assert(baseId==weight2Id);

 const auto legacy=family();
 install(legacy,{0,1});
 assert(manager.loadFamily(legacy,renderer,14,readerInk::physical(1)));
 assert(manager.currentWeight()==1);
 assert(opens.back()==legacy.filePath(legacy.files.front(),1));

 const auto corrupt=family();
 install(corrupt,{0,2,3});
 files[corrupt.filePath(corrupt.files.front(),3)]='B';
 opens.clear();
 assert(manager.loadFamily(corrupt,renderer,14,3));
 assert(manager.currentWeight()==2 && opens.size()==2);
 assert(opens[0]==corrupt.filePath(corrupt.files.front(),3));
 assert(opens[1]==corrupt.filePath(corrupt.files.front(),2));

 const auto baseOnly=family();
 install(baseOnly,{0});
 opens.clear();
 for (int level=0;level<6;++level) {
   assert(manager.loadFamily(baseOnly,renderer,14,readerInk::physical(level)));
   assert(manager.currentWeight()==0 && manager.getFontId("Example")==baseId &&
          opens.size()==static_cast<size_t>(level + 1));
 }
 manager.unloadAll(renderer); assert(renderer.fonts.empty());
 install(full,{0,2,6});
 static const int originalFace=99;
 renderer.insertFont(777,EpdFontFamily(&originalFace,&originalFace,&originalFace,&originalFace));
 for(int physical : {6,2,6}) {
   assert(manager.loadBuiltinFamily(full,renderer,14,physical,777));
   assert(manager.getFontId("Example")==777 && manager.isBuiltinRaster());
   assert(renderer.fonts.size()==1 && renderer.builtinRasterId==777);
   assert(renderer.fonts.at(777).regular!=&originalFace);
 }
 manager.unloadAll(renderer);
 assert(renderer.fonts.size()==1 && renderer.fonts.at(777).regular==&originalFace);
 assert(!manager.isBuiltinRaster());
 assert(!manager.loadBuiltinFamily(full,renderer,14,0,777));
 assert(renderer.fonts.at(777).regular==&originalFace);
 layoutMatches=false;
 assert(!manager.loadBuiltinFamily(full,renderer,14,6,777));
 assert(renderer.fonts.at(777).regular==&originalFace && manager.currentWeight()==0);
 layoutMatches=true;
}
''')
            manager = REPO / 'lib/EpdFont/SdCardFontManager.cpp'
            if (os.environ.get('CROSSPOINT_MUTATE_READER_INK_ID') == '1' or os.environ.get('CROSSPOINT_TEST_MUTATE_WEIGHT_ID')):
                mutated = out / 'manager.cpp'
                mutated.write_text(manager.read_text().replace('hash ^= pointSize;', 'hash ^= weight; hash *= FNV_PRIME; hash ^= pointSize;', 1))
                manager = mutated
            if os.environ.get('CROSSPOINT_MUTATE_SD_FALLBACK'):
                mutated = out / 'fallback.cpp'
                mutated.write_text(manager.read_text().replace('weight >= 2; --weight',
                                                              'weight == requested; --weight', 1))
                manager = mutated
            command = [os.environ.get('CXX', 'c++'), '-std=c++17', '-I' + str(out), '-I' + str(REPO / 'src'),
                       '-I' + str(REPO / 'lib/EpdFont'), str(manager), str(out / 'registry.cpp'),
                       str(out / 'test.cpp'), '-o', str(out / 'test')]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(out / 'test')], capture_output=True, text=True)
            cache_program = os.environ.get('CROSSPOINT_SECTION_CACHE_TEST')
            if cache_program:
                identities = next(line.split('=', 1)[1] for line in result.stdout.splitlines()
                                  if line.startswith('SD_LAYOUT_IDS='))
                cache = subprocess.run([cache_program,
                                        '--gtest_filter=SectionCacheTest.SdVariantSwitchKeepsCachedSectionAndPagePosition'],
                                       env=dict(os.environ, CROSSPOINT_SD_FONT_CACHE_IDS=identities),
                                       capture_output=True, text=True)
                self.assertEqual(cache.returncode, 0, cache.stdout + cache.stderr)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
