import argparse, pathlib, subprocess
repo=pathlib.Path(__file__).resolve().parents[2]
ap=argparse.ArgumentParser()
ap.add_argument('--output',type=pathlib.Path,required=True)
args=ap.parse_args()
args.output.mkdir(parents=True,exist_ok=True)
s=(repo/'src/activities/home/HomeActivity.cpp').read_text()
a=s.index('HomeActivity::CoverSize HomeActivity::cardCoverSize(')
b=s.index('\nvoid HomeActivity::loadCardStats(',a)
code=r'''
#include <cassert>
#include <cstdint>
#include <string>
#include <array>
#include <cstdio>
bool refExists = true;
std::string refHref;
int probes = 0;
struct ImageDimensions { int width = 0, height = 0; };
namespace coverref { bool load(const std::string&, std::string& h) { h = refHref; return refExists; } }
struct Epub {
 Epub(const std::string&, const char*) {}
 std::string getCachePath() { return "/cache"; }
 bool readCoverDimensions(const std::string&, ImageDimensions& d) { ++probes; d={600,900}; return true; }
};
struct FsHelpers { static bool hasEpubExtension(const std::string&) { return true; } };
struct HalFile { void close() {} };
struct { bool openFileForRead(const char*, const std::string&, HalFile&) { return false; } } Storage;
struct UITheme { static std::string getCoverThumbPath(const std::string&, int) { return ""; } };
constexpr int HOME_CARD_COVER_H = 450;
enum class BmpReaderError { Ok };
struct Bitmap { Bitmap(HalFile&) {} BmpReaderError parseHeaders() { return BmpReaderError::Ok; } int getWidth() { return 1; } int getHeight() { return 1; } };
struct HomeActivity {
 struct CoverSize { int width = 0, height = 0; };
 uint8_t cardCoverSizesRead = 0;
 std::array<CoverSize, 1> cardCoverSizes{};
 struct Book { std::string path = "/a.epub", coverBmpPath = "/cache/cover.bmp"; };
 std::array<Book, 1> recentBooks{};
 CoverSize cardCoverSize(int);
};
'''+s[a:b]+r'''
int main() {
 for (int visit=0; visit<3; ++visit) {
   HomeActivity h;
   assert(h.cardCoverSize(0).width == 0 && probes == 0);
 }
 refExists = false;
 HomeActivity missing;
 assert(missing.cardCoverSize(0).width == 600 && probes == 1);
 refExists = true; refHref = "cover.jpg";
 HomeActivity known;
 assert(known.cardCoverSize(0).height == 900 && probes == 2);
 assert(known.cardCoverSize(0).height == 900 && probes == 2);
 std::puts("GREEN empty references skip probes, missing and populated references still probe");
}
'''
p=args.output/'dimensions.cpp';p.write_text(code)
subprocess.run(['c++','-std=c++20','-Wall','-Wextra','-Werror','-UNDEBUG',str(p),'-o',str(p.with_suffix(''))],check=True)
raise SystemExit(subprocess.run([str(p.with_suffix(''))]).returncode)
