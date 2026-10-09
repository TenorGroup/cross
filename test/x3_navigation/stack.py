from pathlib import Path
import argparse
import subprocess

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--work', required=True, type=Path)
args = parser.parse_args()
manager = (root / 'src/activities/ActivityManager.cpp').read_text()


def function(marker):
    start = manager.rfind('\n', 0, manager.index(marker)) + 1
    opening = manager.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (manager[end] == '{') - (manager[end] == '}')
        end += 1
    return manager[start:end]


fixture = r'''
#include <cassert>
#include <cstdio>
#include <memory>
#include "components/X3NavigationPath.h"
namespace tenorchrome { enum class Zone { Recent, File, Favorites, Stats, Settings, Book }; }
using tenorchrome::X3PathIcon;
struct Activity {
  std::string name;
  std::string label;
  X3PathIcon icon;
  bool home = false;
  bool reader = false;
  bool isHomeActivity() const { return home; }
  bool isReaderActivity() const { return reader; }
  std::string x3NavigationLabel() const { return label; }
  X3PathIcon navigationIcon() const { return icon; }
};
struct ActivityManager {
  std::vector<std::unique_ptr<Activity>> stackActivities;
  std::unique_ptr<Activity> currentActivity;
  tenorchrome::Zone zone = tenorchrome::Zone::Settings;
  tenorchrome::Zone footZone() const { return zone; }
  std::vector<tenorchrome::X3PathNode> x3NavigationPath(const char*) const;
  bool x3NavigationIsFilePath() const;
};
BUILDER
FILE_PREDICATE
std::unique_ptr<Activity> activity(const char* name, const char* title, X3PathIcon icon,
                                   bool home = false, bool reader = false) {
  return std::make_unique<Activity>(Activity{name, title, icon, home, reader});
}
int main() {
  ActivityManager manager;
  manager.stackActivities.push_back(activity("Home", "Cài đặt", X3PathIcon::Settings, true));
  manager.stackActivities.push_back(activity("Settings", "Thiết bị", X3PathIcon::Device));
  manager.currentActivity = activity("KeyboardEntry", "Tên thiết bị", X3PathIcon::Keyboard);
  auto path = manager.x3NavigationPath("Tên thiết bị");
  assert(path.size() == 3 && path[0].icon == X3PathIcon::Settings && path[1].icon == X3PathIcon::Device);
  assert(path[2].title == "Tên thiết bị");
  manager.zone = tenorchrome::Zone::Favorites;
  manager.stackActivities.back()->label = "Điều khiển";
  manager.stackActivities.back()->icon = X3PathIcon::Controls;
  manager.currentActivity = activity("ButtonRemap", "Gán lại nút mặt trước", X3PathIcon::Controls);
  path = manager.x3NavigationPath("Gán lại nút mặt trước");
  assert(path[0].icon == X3PathIcon::Settings && path[1].icon == X3PathIcon::Controls);
  manager.currentActivity = activity("Settings", "Điều khiển", X3PathIcon::Controls);
  manager.stackActivities.pop_back();
  path = manager.x3NavigationPath("Điều khiển");
  assert(path[0].icon == X3PathIcon::Settings);
  manager.stackActivities.push_back(activity("Settings", "Thiết bị", X3PathIcon::Device));
  manager.zone = tenorchrome::Zone::Settings;
  manager.stackActivities.back()->label = "Hiển thị";
  manager.stackActivities.back()->icon = X3PathIcon::Display;
  manager.currentActivity = activity("StatusBarSettings", "Thanh trạng thái", X3PathIcon::Status);
  path = manager.x3NavigationPath("Thanh trạng thái");
  assert(path[1].icon == X3PathIcon::Display && path[2].icon == X3PathIcon::Status);
  manager.stackActivities.push_back(activity("StatusBarSettings", "Thanh trạng thái", X3PathIcon::Status));
  manager.currentActivity = activity("SettingsChoices", "Đồng hồ", X3PathIcon::More);
  path = manager.x3NavigationPath("Đồng hồ");
  assert(path.size() == 4 && path[2].icon == X3PathIcon::Status);
  manager.stackActivities.pop_back();
  manager.stackActivities.pop_back();
  manager.zone = tenorchrome::Zone::Recent;
  manager.stackActivities.push_back(activity("EpubReader", "Sách", X3PathIcon::Book, false, true));
  manager.stackActivities.push_back(activity("EpubReaderMenu", "Trình đọc", X3PathIcon::Reading));
  manager.currentActivity = activity("TextSettings", "Cài đặt văn bản", X3PathIcon::Reading);
  path = manager.x3NavigationPath("Cài đặt văn bản");
  assert(path.size() == 3 && path[0].icon == X3PathIcon::Book && path[1].title == "Trình đọc");
  manager.currentActivity = activity("FileBrowser", "/books/folder", X3PathIcon::File);
  path = manager.x3NavigationPath("/books/folder");
  assert(path.size() == 2 && path[0].icon == X3PathIcon::File && path[1].title == "/books/folder");
  manager.stackActivities.clear();
  manager.currentActivity = activity("ReadingHistory", "Lịch sử", X3PathIcon::Stats);
  for (const auto zone : {tenorchrome::Zone::Recent, tenorchrome::Zone::File,
                         tenorchrome::Zone::Favorites, tenorchrome::Zone::Stats,
                         tenorchrome::Zone::Settings, tenorchrome::Zone::Book}) {
    manager.zone = zone;
    path = manager.x3NavigationPath("Lịch sử");
    assert(path.size() == 2);
    assert(static_cast<int>(path[0].icon) == static_cast<int>(zone));
  }
  std::puts("GREEN: actual stack builder, Settings/Device/Keyboard, changed parent, nested choices, reader, File, 6 zones");
}
'''
fixture = fixture.replace('BUILDER', function('ActivityManager::x3NavigationPath('))
fixture = fixture.replace('FILE_PREDICATE', function('ActivityManager::x3NavigationIsFilePath('))
args.work.mkdir(parents=True, exist_ok=True)
source = args.work / 'stack.cpp'
binary = args.work / 'stack'
source.write_text(fixture)
subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
                '-I', str(root / 'src'), str(source), '-o', str(binary)], check=True)
raise SystemExit(subprocess.run([str(binary)]).returncode)
