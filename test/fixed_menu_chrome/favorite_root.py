from pathlib import Path
import argparse
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--work', type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
source = (root / 'src/activities/home/HomeRows.cpp').read_text()
start = source.index('Favorites favorites() {')
end = source.index('\nSettingsGroups settingsGroups()', start)
unit = r'''
#include <cassert>
#include <string>
#include <vector>
enum class StrId { STR_NONE_OPT };
constexpr int STR_HOME_TAB_FOLDER = 1, STR_DICT_NOT_FOUND = 2, STR_FAVORITE_SD_ROOT = 3;
const char* rootName = "Thẻ nhớ";
const char* tr(int key) { return key == 1 ? "File" : key == 3 ? rootName : "Missing"; }
struct I18n { const char* get(StrId) { return "Setting"; } } I18N;
std::string utf8ComposeNfc(const std::string& text) { return text; }
namespace menucustom {
struct Pin { std::string key; const char* data() const { return key.c_str(); } };
struct State { int pinCount = 3; Pin pins[3] = {{"folder/root"}, {"bookid/book"}, {"folder/missing"}}; };
State& state() { static State data; return data; }
}
namespace filefavorites {
bool isFileKey(const std::string&) { return true; }
std::string pathFor(const std::string& key) {
  return key == "folder/root" ? "/" : key == "bookid/book" ? "/books/probe.epub" : "";
}
}
struct FontSystem { const int& registry() { static int data = 0; return data; } } sdFontSystem;
namespace menufavorites {
StrId label(const std::string&) { return StrId::STR_NONE_OPT; }
std::string value(const std::string&, const int*) { return ""; }
}
namespace homerows {
struct Favorites { std::vector<std::string> keys, values, labels; };
PRODUCTION
}
int main() {
  for (const char* name : {"Thẻ nhớ", "SD card", "存储卡"}) {
  rootName = name;
  const auto rows = homerows::favorites();
  assert(rows.labels.size() == 3);
  assert(rows.labels[0] == rootName);
  assert(rows.values[0] == "File");
  assert(rows.labels[1] == "probe.epub");
  assert(rows.labels[2] == "Missing");
  }
}
'''.replace('PRODUCTION', source[start:end])
args.work.mkdir(parents=True, exist_ok=True)
path = args.work / 'favorite.cpp'
path.write_text(unit)
program = args.work / 'favorite'
subprocess.run(['c++', '-std=c++20', '-UNDEBUG', str(path), '-o', str(program)], check=True)
subprocess.run([str(program)], check=True)
for locale, expected in [('vietnamese', 'Thẻ nhớ'), ('english', 'SD card'), ('chinese', '存储卡')]:
    assert f'STR_FAVORITE_SD_ROOT: "{expected}"' in (root / f'lib/I18n/translations/{locale}.yaml').read_text()
print('GREEN: root, file and missing favorite names')
