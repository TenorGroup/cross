from pathlib import Path
import argparse
import re
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--work', type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
source = (root / 'lib/KOReaderSync/KOReaderSyncClient.cpp').read_text()
method = source[source.index('const char* KOReaderSyncClient::errorString('):]
unit = r'''
#include <cassert>
#include <cstring>
#include <string>
#include <I18n.h>
#include <KOReaderSyncClient.h>
PRODUCTION
int main() {
  for (int code = 0; code <= KOReaderSyncClient::USER_EXISTS + 1; ++code) {
    const auto error = static_cast<KOReaderSyncClient::Error>(code);
    I18N.setLanguage(Language::EN);
    const std::string english = KOReaderSyncClient::errorString(error);
    I18N.setLanguage(Language::VI);
    const std::string vietnamese = KOReaderSyncClient::errorString(error);
    I18N.setLanguage(Language::ZH_HANS);
    const std::string chinese = KOReaderSyncClient::errorString(error);
    assert(!english.empty() && !vietnamese.empty() && !chinese.empty());
    assert(vietnamese != english && chinese != english);
  }
}
'''.replace('PRODUCTION', method)
args.work.mkdir(parents=True, exist_ok=True)
path = args.work / 'errors.cpp'
path.write_text(unit)
program = args.work / 'errors'
translations = root / 'lib/I18n'
subprocess.run(['c++', '-std=c++20', '-UNDEBUG', '-I', str(translations), '-I', str(root / 'lib/KOReaderSync'),
                str(path), str(translations / 'I18n.cpp'), str(translations / 'I18nStrings.cpp'),
                '-o', str(program)], check=True)
subprocess.run([str(program)], check=True)
for path in (root / 'src/activities').rglob('*.cpp'):
    literals = re.findall(r'(?:errorMessage_?|statusMessage|statusText_|errorText|failureReason)\s*=\s*(?:std::string\()?"[A-Za-z][^"\n]*"', path.read_text())
    assert not literals, (path, literals)
print('GREEN: 10 backend results in VI/EN/ZH and no English error literals in activities')
