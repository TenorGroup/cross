"""Home's idle cover wiring (X3), run from the production source of HomeActivity.cpp:
every tenor/ink slot shown is queued (the shelf's too), none on the 1 bit branch; a step waits for
quiet, never waits for the render task, holds the CPU at full speed, gives the card's snapshot
back before the step asks for memory, draws again only after a sharp cover, and waits after a
heap floor refusal.

  run.py --output <dir> [--source src/activities/home/HomeActivity.cpp]
"""
import argparse, pathlib, subprocess

p = pathlib.Path(__file__).resolve().parent
repo = p.parents[1]
a = argparse.ArgumentParser()
a.add_argument('--source', type=pathlib.Path, default=repo / 'src/activities/home/HomeActivity.cpp')
a.add_argument('--output', type=pathlib.Path, required=True)
args = a.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
t = args.source.read_text()


def function(signature):
    start = t.index(signature)
    brace = t.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (t[end] == '{') - (t[end] == '}')
        end += 1
    return t[start:end] + '\n'


harness = r'''
#include <IdleWork.h>
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
namespace coverink { enum class Outcome : uint8_t { Nothing, Sharp, Stopped, NoHeap, Later, Failed, Gone }; }
static constexpr bool kInkX3 = true;
uint32_t now = 10000;
uint32_t millis() { return now; }
struct { bool pressed = false; bool wasAnyPressed() const { return pressed; } bool wasAnyReleased() const { return false; } } mappedInput;
struct { bool usb = false; bool isUsbConnected() const { return usb; } bool rawInputActive() const { return false; } } gpio;
int locks = 0;
struct HalPowerManager {
  int percent = 80;
  int getBatteryPercentage() const { return percent; }
  struct Lock { Lock() { ++locks; } ~Lock() { --locks; } };
} powerManager;
bool renderBusy = false;
struct RenderLock { struct TryTake {}; explicit RenderLock(TryTake) {} bool acquired() const { return !renderBusy; } };
struct { unsigned getFreeHeap() const { return 1; } unsigned getMaxAllocHeap() const { return 1; } } ESP;
#define LOG_INF(...) ((void)0)
struct FsHelpers { static bool hasEpubExtension(const std::string& s) { return s.size() > 5 && s.ends_with(".epub"); } };
idlework::Budget idleBudget{};
uint64_t idleClockUs() { return uint64_t(now) * 1000; }
struct HomeRecentRect { int x = 0, y = 0, w = 0, h = 0; };
struct RecentBook { std::string path, coverBmpPath; };
enum class Tab { RECENT, STATS };
'''
fnvs = t[t.index('uint32_t fnv(uint32_t hash, const void* data'):t.index('\n// Books whose missing card thumbnail')]
home = r'''
struct HomeActivity {
  std::vector<RecentBook> recentBooks;
  Tab activeTabId = Tab::RECENT;
  bool ink = true, homeGraySelected = true, coverRendered = true, snapshot = true;
  struct InkSlot { HomeRecentRect rect; int book = -1; };
  InkSlot inkSlots[4]{};
  int inkSlotCount = 0;
  idlework::Queue<> idleQueue;
  uint32_t idleQuietSince = 0, idleRetryAt = 0;
  uint8_t idleOutcome = 0;
  int updates = 0;
  bool homeInkEnabled() const { return ink; }
  void freeCoverBuffer() { snapshot = false; }
  void requestUpdate() { ++updates; }
  void queueIdleCovers();
  void idleStep();
  static idlework::Step coverStep(const idlework::Job& job, const idlework::Stop& stop);
};
coverink::Outcome next = coverink::Outcome::Sharp;
int runs = 0;
bool snapshotDuringRun = true;
int locksDuringRun = 0;
idlework::Step HomeActivity::coverStep(const idlework::Job& job, const idlework::Stop&) {
  auto& home = *static_cast<HomeActivity*>(job.owner);
  ++runs;
  snapshotDuringRun = home.snapshot;
  locksDuringRun = locks;
  home.idleOutcome = static_cast<uint8_t>(next);
  return next == coverink::Outcome::Stopped || next == coverink::Outcome::NoHeap ? idlework::Step::Stopped : idlework::Step::Done;
}
'''
cases = r'''
int main() {
  HomeActivity h;
  h.recentBooks = {{"/a.epub", "/c/a/thumb_[HEIGHT].bmp"}, {"/b.epub", "/c/b/thumb_[HEIGHT].bmp"},
                   {"/c.txt", "/c/c/thumb_[HEIGHT].bmp"}, {"/d.epub", ""}, {"/e.epub", "/c/e/thumb_[HEIGHT].bmp"}};
  // The shelf: the card and 3 shelf covers, all queued but the text file and the book with no cover.
  h.inkSlotCount = 4;
  h.inkSlots[0] = {{24, 16, 298, 450}, 0};
  h.inkSlots[1] = {{30, 600, 96, 140}, 1};
  h.inkSlots[2] = {{180, 600, 96, 140}, 2};
  h.inkSlots[3] = {{330, 600, 96, 140}, 3};
  h.queueIdleCovers();
  assert(h.idleQueue.size() == 2);
  h.inkSlots[2].book = 4;
  h.queueIdleCovers();
  assert(h.idleQueue.size() == 3 && h.idleQuietSince == now);
  std::puts("GREEN every tenor/ink slot shown is queued, the shelf's too; no text file, no book without a cover");
  h.ink = false;
  h.queueIdleCovers();
  assert(h.idleQueue.empty());
  h.ink = true;
  std::puts("GREEN the 1 bit branch (ugly shell, X3 layout off) queues nothing");

  h.inkSlotCount = 1;
  h.queueIdleCovers();
  h.idleStep();
  assert(runs == 0);  // the frame was just drawn
  now += 3000;
  renderBusy = true;
  h.idleStep();
  assert(runs == 0);  // a frame in progress: no waiting for it
  renderBusy = false;
  mappedInput.pressed = true;
  h.idleStep();
  assert(runs == 0 && h.idleQuietSince == now);
  mappedInput.pressed = false;
  now += 3000;
  h.activeTabId = Tab::STATS;
  h.idleStep();
  assert(runs == 0);
  h.activeTabId = Tab::RECENT;
  powerManager.percent = 20;
  h.idleStep();
  assert(runs == 0);
  gpio.usb = true;
  h.idleStep();
  assert(runs == 1 && locksDuringRun == 1 && !snapshotDuringRun && locks == 0 && !h.coverRendered && h.updates == 1);
  std::puts("GREEN a step waits for 3 s of quiet and the Recent tab, skips a busy frame, a key restarts the wait, USB lifts the battery floor");
  std::puts("GREEN during the step the CPU is held and the card's snapshot is already given back; a sharp cover draws again");

  h.queueIdleCovers();  // the redraw queues the shown cover again; done this visit, it is not
  now += 3000;
  h.idleStep();
  assert(runs == 1);
  h.inkSlots[0].book = 1;
  h.queueIdleCovers();
  now += 3000;
  next = coverink::Outcome::NoHeap;
  h.idleStep();
  assert(runs == 2 && h.updates == 1 && h.idleRetryAt == now + 30000);
  now += 10000;
  h.idleStep();
  assert(runs == 2);
  now += 20000;
  next = coverink::Outcome::Nothing;
  h.idleStep();
  assert(runs == 3 && h.updates == 1 && h.idleQueue.empty());
  std::puts("GREEN a done cover is not queued again this visit; a heap floor refusal waits 30 s; nothing new draws nothing");
  return 0;
}
'''
source = (harness + fnvs + home + function('void HomeActivity::queueIdleCovers()') + function('void HomeActivity::idleStep()') + cases)
(args.output / 'generated.cpp').write_text(source)
subprocess.run(['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror', '-UNDEBUG', '-I', str(repo / 'lib/IdleWork'),
                str(args.output / 'generated.cpp'), '-o', str(args.output / 'test')], check=True)
r = subprocess.run([str(args.output / 'test')], capture_output=True, text=True)
print(r.stdout, end='')
print(r.stderr, end='')
raise SystemExit(r.returncode)
