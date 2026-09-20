"""Exercise real registry/manager with equal content hashes and failing files."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]


class ManagerTest(unittest.TestCase):
    def test_public_paths_identity_fallback_and_unweighted_ui(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            (out / 'Logging.h').write_text('#pragma once\n#define LOG_DBG(...)\n#define LOG_ERR(...)\n')
            (out / 'Files.h').write_text('''#pragma once
#include <map>
#include <string>
#include <vector>
inline std::map<std::string, char> files;
inline std::vector<std::string> opens;
''')
            (out / 'HalStorage.h').write_text('''#pragma once
#include "Files.h"
struct StorageFake { bool exists(const char* path) { return files.count(path); } };
inline StorageFake Storage;
''')
            (out / 'EpdFontFamily.h').write_text('''#pragma once
struct EpdFontFamily { EpdFontFamily(const int*,const int*,const int*,const int*){} };
''')
            (out / 'SdCardFont.h').write_text('''#pragma once
#include "Files.h"
#include <cstdint>
struct SdCardFont {
 bool load(const char* path) { opens.push_back(path); return files.count(path) && files.at(path)=='G'; }
 uint32_t contentHash() const { return 12345; }
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
 const auto& getFontMap() const { return fonts; }
 void registerSdCardFont(int,SdCardFont*){}
 void insertFont(int id,EpdFontFamily font) { fonts.emplace(id,font); }
 void clearFallbackFonts(){}
 void clearSdCardFonts(){}
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
int main() {
 SdCardFontFamilyInfo family;
 family.name="Example"; family.stems={"Example"}; family.files={{14,0,0}};
 const auto& file=family.files.front();
 for (int weight=0;weight<=4;++weight) {
   const std::string path="/.fonts/Example/"+(weight?"weight-"+std::to_string(weight)+"/":"")+"Example_14.cpfont";
   files[path]='G';
   assert(family.filePath(file,weight)==path);
 }
 assert(readerInk::publicMask(family.weights(file))==15);
 GfxRenderer renderer; SdCardFontManager manager; int ids[4];
 for (int level=0;level<4;++level) {
   const int physical=readerInk::physical(level);
   assert(manager.loadFamily(family,renderer,15,physical));
   assert(manager.currentPointSize()==14 && manager.currentWeight()==physical);
   assert(readerInk::publicFromPhysical(manager.currentWeight())==level);
   ids[level]=manager.getFontId("Example");
   assert(opens.back()==family.filePath(file,physical));
   assert(renderer.fonts.size()==1);
 }
 for(int i=0;i<4;++i)for(int j=0;j<i;++j)assert(ids[i]!=ids[j]);
 // Legacy physical weight1 remains loadable and separately identified.
 assert(manager.loadFamily(family,renderer,14,1));
 const int legacyId=manager.getFontId("Example");
 for (int id:ids) assert(legacyId!=id);
 assert(manager.loadFamily(family,renderer,14,4));
 const int ui=manager.loadFamilyExtraSize(family,renderer,14);
 assert(ui==ids[0] && renderer.fonts.size()==2);
 assert(manager.loadFamilyExtraSize(family,renderer,14)==ui);
 manager.unloadExtraSizes(renderer); assert(renderer.fonts.size()==1);
 files[family.filePath(file,4)]='B';
 assert(manager.loadFamily(family,renderer,14,4));
 assert(manager.currentWeight()==0 && manager.getFontId("Example")==ids[0]);
 files.erase(family.filePath(file,3));
 assert(manager.loadFamily(family,renderer,14,3));
 assert(manager.currentWeight()==0 && manager.currentFamilyName()=="Example");
 assert(readerInk::publicMask(family.weights(file))==11);
 manager.unloadAll(renderer); assert(renderer.fonts.empty());
}
''')
            manager = REPO / 'lib/EpdFont/SdCardFontManager.cpp'
            if (os.environ.get('CROSSPOINT_MUTATE_READER_INK_ID') == '1' or os.environ.get('CROSSPOINT_TEST_MUTATE_WEIGHT_ID')):
                mutated = out / 'manager.cpp'
                mutated.write_text(manager.read_text().replace('if (weight) {', 'if (false) {', 1))
                manager = mutated
            command = [os.environ.get('CXX', 'c++'), '-std=c++17', '-I' + str(out), '-I' + str(REPO / 'src'),
                       '-I' + str(REPO / 'lib/EpdFont'), str(manager), str(out / 'registry.cpp'),
                       str(out / 'test.cpp'), '-o', str(out / 'test')]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(out / 'test')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
