"""Check that the X3 layout retains the download and batch result information."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--work', type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
source = (root / 'src/activities/settings/FontDownloadActivity.cpp').read_text()
method = source.split('std::string FontDownloadActivity::x3InfoText() const {', 1)[1]
method = 'std::string FontDownloadActivity::x3InfoText() const {' + method.split('\nvoid FontDownloadActivity::renderX3State()', 1)[0]
unit = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
enum Key { STR_LOADING_FONT_LIST, STR_FONT_INSTALLED, STR_FONT_INSTALL_FAILED,
           STR_DOWNLOADING, STR_FONT_BATCH_DONE, STR_FONT_BATCH_FAILED,
           STR_FONT_BATCH_DOWNLOADING, STR_FONT_BATCH_TOTAL, STR_FONT_PACK_INSTALLING };
const char* tr(Key key) {
  static const char* words[] = {"Loading", "Installed", "Failed", "Downloading",
      "Done %u/%u", "Failure %u/%u", "Downloading %s/%s", "Total %u/%u %u%%", "Installing"};
  return words[key];
}
struct Family { const char* name; uint64_t totalSize; const char* failureReason; unsigned requiredMb; };
struct FontDownloadActivity {
  enum State { LOADING_MANIFEST, DOWNLOADING, COMPLETE, ERROR } state_ = COMPLETE;
  bool batchResult_ = true, batchRunning_ = true, installingPack_ = false;
  unsigned batchSuccessCount_ = 2, batchFamilyCount_ = 3, batchFailureCount_ = 1, batchFamilyIndex_ = 2;
  int resultFailureIndex_ = 1, downloadingFamilyIndex_ = 0, currentFileIndex_ = 0, currentFileTotal_ = 1;
  uint64_t familyDownloadedBytes_ = 100, fileProgress_ = 100, fileTotal_ = 300;
  uint64_t batchTotalBytes_ = 1000, batchDownloadedBytes_ = 400;
  std::string errorMessage_ = "Transfer interrupted";
  std::vector<Family> families_ = {{"NotoSerif", 400, nullptr, 0}, {"Literata", 600, "Need %u MB", 8}};
  const char* str(const char* value) const { return value; }
  std::string formatSize(uint64_t value) const { return std::to_string(value); }
  std::string x3InfoText() const;
};
PRODUCTION
int main() {
  FontDownloadActivity page;
  auto text = page.x3InfoText();
  assert(text.find("Done 2/3") != std::string::npos);
  assert(text.find("Failure 1/1") != std::string::npos);
  assert(text.find("Literata") != std::string::npos);
  assert(text.find("Need 8 MB") != std::string::npos);
  page.state_ = FontDownloadActivity::DOWNLOADING;
  text = page.x3InfoText();
  assert(text.find("Downloading 200/400") != std::string::npos);
  assert(text.find("Total 2/3 50%") != std::string::npos);
  assert(text.find("500 / 1000") != std::string::npos);
  page.installingPack_ = true;
  assert(page.x3InfoText().find("Installing") != std::string::npos);
  page.state_ = FontDownloadActivity::ERROR;
  assert(page.x3InfoText().find("Transfer interrupted") != std::string::npos);
  page.state_ = FontDownloadActivity::COMPLETE;
  page.batchResult_ = false;
  assert(page.x3InfoText() == "Installed");
  std::puts("GREEN: X3 retains batch outcomes, transfer totals and installation phase");
}
'''.replace('PRODUCTION', method)
args.work.mkdir(parents=True, exist_ok=True)
path = args.work / 'x3-merge.cpp'
path.write_text(unit)
program = args.work / 'x3-merge'
subprocess.run(['c++', '-std=c++20', '-UNDEBUG', str(path), '-o', str(program)], check=True)
subprocess.run([str(program)], check=True)
