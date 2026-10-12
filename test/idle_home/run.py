"""Home's idle cover wiring (X3), run from the production source of HomeActivity.cpp: the card's
tenor/ink cover is queued at the depth Home draws, none on the 1 bit GrayThumb branch; a step waits
for quiet (a key held too), Home in front and the hour's steps, decides that before it tries the
render lock, never waits for the render task, holds the CPU at full speed, draws again only after a
sharp cover, keeps the card's snapshot when it has nothing to do, backs off after a heap floor
refusal, and is tried again when Home comes back to the front. The card restores a cut replacement
(coverink::restore) before it opens its cover. A tick that wrote a cover placeholder starts no step.

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


fnvs = t[t.index('uint32_t fnv(uint32_t hash, const void* data'):t.index('\n// Books whose missing card thumbnail')]
harness = r'''
#include <IdleWork.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
#include <set>
namespace coverink { enum class Outcome : uint8_t { Nothing, Sharp, Stopped, NoHeap, Later, Failed, Gone }; }
static constexpr bool kInkX3 = true;
uint32_t now = 10000;
uint32_t millis() { return now; }
struct { bool pressed = false; bool wasAnyPressed() const { return pressed; } bool wasAnyReleased() const { return false; } } mappedInput;
struct { bool usb = false, held = false; bool isUsbConnected() const { return usb; } bool rawInputActive() const { return held; } } gpio;
int locks = 0, tries = 0;
struct HalPowerManager {
  int percent = 80;
  int getBatteryPercentage() const { return percent; }
  struct Lock { Lock() { ++locks; } ~Lock() { --locks; } };
} powerManager;
bool renderBusy = false;
struct RenderLock { struct TryTake {}; explicit RenderLock(TryTake) { ++tries; } bool acquired() const { return !renderBusy; } };
#define LOG_INF(...) ((void)0)
struct FsHelpers { static bool hasEpubExtension(const std::string& s) { return s.size() > 5 && s.ends_with(".epub"); } };
idlework::Budget idleBudget{};
uint64_t idleClockUs() { return uint64_t(now) * 1000; }
struct HomeRecentRect { int x = 0, y = 0, w = 0, h = 0; };
struct RecentBook { std::string path, coverBmpPath; };
enum class Tab { RECENT, STATS };
struct UiTabListActivity { void onResume() {} };
'''
# What the real coverStep (checked below) reaches: the book's cache, cover.ref, the step.
step_stubs = r'''
#include <TenorInk.h>
std::set<std::string> refs;  // book caches holding a cover.ref
struct Epub {
  std::string cache;
  Epub(const std::string& path, const std::string& dir) : cache(dir + "/epub_" + std::to_string(path.size())) {}
  const std::string& getCachePath() const { return cache; }
};
namespace coverref { bool load(const std::string& cache, std::string& href) {
  if (!refs.count(cache)) return false;
  href = "OEBPS/images/cover.jpg";
  return true;
} }
namespace progluma { enum class Result : uint8_t { Done }; class Sniffer; }
'''
step_build = r'''
namespace coverink {
struct Source {
  progluma::Result (*sniff)(void*, progluma::Sniffer&, const idlework::Stop&) = nullptr;
  progluma::Result (*copy)(void*, const std::string&, const idlework::Stop&) = nullptr;
  void (*release)(void*) = nullptr;
  void* user = nullptr;
};
std::string builtDir, builtPath, builtHref;
int builtFour = -1;
Outcome build(const std::string& dir, const std::string& path, int, int, bool four, const Source& s,
              const idlework::Stop&, const char** why) {
  builtDir = dir; builtPath = path; builtFour = four;
  builtHref = static_cast<BookCover*>(s.user)->href;
  *why = "built";
  return Outcome::Sharp;
}
}  // namespace coverink
progluma::Result sniffCover(void*, progluma::Sniffer&, const idlework::Stop&) { return progluma::Result::Done; }
progluma::Result copyCover(void*, const std::string&, const idlework::Stop&) { return progluma::Result::Done; }
'''
home = r'''
struct HomeActivity : UiTabListActivity {
  std::vector<RecentBook> recentBooks;
  Tab activeTabId = Tab::RECENT;
  struct { bool active = false; bool isActive() const { return active; } } rowMenu;
  bool ink = true, homeGraySelected = false, coverRendered = true, snapshot = true;
  struct InkSlot { HomeRecentRect rect; int book = -1; };
  InkSlot inkSlots[4]{};
  int inkSlotCount = 0;
  idlework::Queue<> idleQueue;
  uint32_t idleQuietSince = 0, idleRetryAt = 0, idleBackoffMs = 30000;
  uint8_t idleOutcome = 0;
  const char* idleWhy = "";
  int updates = 0;
  bool homeInkEnabled() const { return ink; }
  void freeCoverBuffer() { snapshot = false; }
  void requestUpdate() { ++updates; }
  void queueIdleCovers();
  void idleStep();
  void idleRelease();
  void onResume();
  static idlework::Step coverStep(const idlework::Job& job, const idlework::Stop& stop);
  static idlework::Step realStep(const idlework::Job& job, const idlework::Stop& stop);
};
coverink::Outcome next = coverink::Outcome::Sharp;
int runs = 0;
bool snapshotDuringRun = true;
int locksDuringRun = 0;
// As coverStep through coverink::build: a step that takes memory first gives the snapshot back.
idlework::Step HomeActivity::coverStep(const idlework::Job& job, const idlework::Stop&) {
  auto& home = *static_cast<HomeActivity*>(job.owner);
  ++runs;
  if (next != coverink::Outcome::Nothing) home.idleRelease();
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
  // X3 Home: the card's one cover, 1 bit since v1.0.57 (homeGraySelected false): queued at that depth.
  h.inkSlotCount = 1;
  h.inkSlots[0] = {{24, 16, 298, 450}, 0};
  h.queueIdleCovers();
  assert(h.idleQueue.size() == 1 && h.idleQueue.front().arg[3] == 0 && h.idleQueue.front().arg[1] == 298);
  assert(h.idleQuietSince == now);
  std::puts("GREEN X3 Home queues its card's cover at the depth it draws (1 bit), after each frame");
  // A guard, not reached on the X3 today (one slot): more slots would all be queued.
  h.inkSlotCount = 4;
  h.inkSlots[1] = {{30, 600, 96, 140}, 1};
  h.inkSlots[2] = {{180, 600, 96, 140}, 2};
  h.inkSlots[3] = {{330, 600, 96, 140}, 3};
  h.queueIdleCovers();
  assert(h.idleQueue.size() == 2);
  h.inkSlots[2].book = 4;
  h.queueIdleCovers();
  assert(h.idleQueue.size() == 3);
  std::puts("GREEN (guard) more tenor/ink slots would all be queued, no text file, no book without a cover");
  h.ink = false;
  h.queueIdleCovers();
  assert(h.idleQueue.empty());
  h.ink = true;
  std::puts("GREEN the 1 bit GrayThumb branch (ugly shell, X3 layout off) queues nothing");

  h.inkSlotCount = 1;
  h.queueIdleCovers();
  h.idleStep();
  assert(runs == 0 && tries == 0);  // the frame was just drawn: not even the lock is tried
  now += 3000;
  renderBusy = true;
  h.idleStep();
  assert(runs == 0 && tries == 1);  // a frame in progress: no waiting for it
  renderBusy = false;
  mappedInput.pressed = true;
  h.idleStep();
  assert(runs == 0 && h.idleQuietSince == now);
  mappedInput.pressed = false;
  gpio.held = true;  // held from before: no edge, still not idle
  now += 3000;
  h.idleStep();
  now += 3000;
  h.idleStep();
  assert(runs == 0 && h.idleQuietSince == now);
  gpio.held = false;
  now += 3000;
  h.rowMenu.active = true;
  h.idleStep();
  h.rowMenu.active = false;
  h.activeTabId = Tab::STATS;
  h.idleStep();
  h.activeTabId = Tab::RECENT;
  powerManager.percent = 20;
  h.idleStep();
  assert(runs == 0 && tries == 1);
  std::puts("GREEN a key pressed, let go or held past 3 s, a menu over Home, another tab, a low battery: no step, no lock tried");
  gpio.usb = true;
  h.idleStep();
  assert(runs == 1 && locksDuringRun == 1 && !snapshotDuringRun && locks == 0 && !h.coverRendered && h.updates == 1);
  std::puts("GREEN on USB a step runs at full CPU speed; a sharp cover draws again");

  h.queueIdleCovers();  // the redraw queues the shown cover again; done this visit, it is not
  now += 3000;
  h.idleStep();
  assert(runs == 1);
  // A step with nothing to do keeps the card's snapshot and draws nothing.
  h.inkSlots[0].book = 1;
  h.snapshot = true;
  h.coverRendered = true;
  h.queueIdleCovers();
  now += 3000;
  next = coverink::Outcome::Nothing;
  h.idleStep();
  assert(runs == 2 && h.snapshot && h.coverRendered && h.updates == 1 && h.idleQueue.empty());
  std::puts("GREEN a done cover is not queued again; a step with nothing to do keeps the snapshot and draws nothing");

  // Back in front (onResume): what ended this visit is queued again.
  h.onResume();
  h.queueIdleCovers();
  assert(h.idleQueue.size() == 1);
  // The heap floor: wait 30 s, then 60 s, 120 s, ... up to 10 minutes; another outcome starts over.
  next = coverink::Outcome::NoHeap;
  const uint32_t waits[] = {30000, 60000, 120000, 240000, 480000, 600000, 600000};
  for (const uint32_t wait : waits) {
    now += 3000;
    h.idleQuietSince = now - 3000;
    h.idleStep();
    const int ran = runs;
    assert(h.idleRetryAt == now + wait);
    now += wait - 1;
    h.idleQuietSince = now - 3000;
    h.idleStep();
    assert(runs == ran);
    now += 1;
  }
  next = coverink::Outcome::Later;
  now += 3000;
  h.idleQuietSince = now - 3000;
  h.idleStep();
  assert(h.idleBackoffMs == 30000 && h.idleQueue.empty());
  std::puts("GREEN back in front a cover is tried again; the heap floor waits 30 s doubling up to 10 minutes");

  // 120 steps an hour, on the cable too.
  h.onResume();
  h.queueIdleCovers();
  idleBudget = {};
  const int before = runs;
  next = coverink::Outcome::Stopped;
  for (int i = 0; i < 200; ++i) {
    now += 3000;
    h.idleQuietSince = now - 3000;
    h.idleStep();
  }
  assert(runs - before == 120);
  std::puts("GREEN no more than 120 steps an hour, on USB too");

  // The real coverStep: a book without cover.ref is left, and says so; with it, the step builds the
  // tenor/ink cover Home draws (1 bit) in that book's cache, from the cover.ref's path.
  idlework::Job job;
  job.owner = &h;
  job.arg[0] = 0; job.arg[1] = 298; job.arg[2] = 450; job.arg[3] = 0;
  h.recentBooks[0] = {"/v1015/p58-1.epub", "/.crosspoint/epub_17/thumb2_[HEIGHT].bmp"};
  idlework::Stop stop;
  assert(HomeActivity::realStep(job, stop) == idlework::Step::Done && std::string(h.idleWhy) == "no-cover-ref");
  assert(coverink::builtPath.empty());
  refs.insert("/.crosspoint/epub_17");
  assert(HomeActivity::realStep(job, stop) == idlework::Step::Done && std::string(h.idleWhy) == "built");
  assert(coverink::builtDir == "/.crosspoint/epub_17" && coverink::builtPath == "/.crosspoint/epub_17/ink2_1_298x450.bmp");
  assert(coverink::builtFour == 0 && coverink::builtHref == "OEBPS/images/cover.jpg");
  std::puts("GREEN the real step: no cover.ref says so; with it the 1 bit cover of that book's cache is built");
  return 0;
}
'''
# The card restores a cut replacement (coverink::restore) before it opens its tenor/ink cover.
read = t.index('if (!Storage.openFileForRead("HOME", path, file)) continue;')
assert 'coverink::restore(path);' in t[read - 120:read], 'the card opens its cover without coverink::restore'
print('GREEN the card restores a cut replacement before it opens its tenor/ink cover')
source = (harness + fnvs + home + function('void HomeActivity::queueIdleCovers()') + function('void HomeActivity::idleStep()')
          + function('void HomeActivity::idleRelease()') + function('void HomeActivity::onResume()')
          + step_stubs + t[t.index('struct BookCover {'):t.index('};', t.index('struct BookCover {')) + 3] + step_build
          + function('idlework::Step HomeActivity::coverStep(').replace('HomeActivity::coverStep(', 'HomeActivity::realStep(')
          + cases)
(args.output / 'generated.cpp').write_text(source)
subprocess.run(['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror', '-UNDEBUG', '-I', str(repo / 'lib/IdleWork'),
                '-I', str(repo / 'lib/JpegToBmpConverter'),
                str(args.output / 'generated.cpp'), '-o', str(args.output / 'test')], check=True)
r = subprocess.run([str(args.output / 'test')], capture_output=True, text=True)
print(r.stdout, end='')
print(r.stderr, end='')
if r.returncode:
    raise SystemExit(r.returncode)

# onTick from the production source: a tick in which the cover placeholder was written (tickHolders asked
# for a redraw) starts no idle step, so the placeholder is drawn before a long build begins.
tick = r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
uint32_t now = 10000;
uint32_t millis() { return now; }
void saveAppState() {}
constexpr uint32_t CARD_THUMB_IDLE_MS = 1000;
struct { bool wasAnyPressed() const { return false; } bool wasAnyReleased() const { return false; } } mappedInput;
struct HomeActivity {
  std::atomic<bool> wakeStatePending{false};
  std::atomic<int> thumbWanted{-1};
  std::atomic<uint32_t> thumbWantedAtMs{0};
  bool holderWrote = false;
  int steps = 0, thumbs = 0;
  bool tickHolders() { return holderWrote; }
  void idleStep() { ++steps; }
  void tickHomeGhost() {}
  void writeMissingThumb(int) { ++thumbs; }
  void onTick();
};
''' + function('void HomeActivity::onTick()') + r'''
int main() {
  HomeActivity h;
  h.holderWrote = true;
  h.onTick();
  assert(h.steps == 0);
  h.holderWrote = false;
  h.onTick();
  assert(h.steps == 1);
  std::puts("GREEN a tick that wrote the cover placeholder starts no idle step; the next one does");
  return 0;
}
'''
(args.output / 'tick.cpp').write_text(tick)
subprocess.run(['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror', '-UNDEBUG', str(args.output / 'tick.cpp'), '-o',
                str(args.output / 'tick')], check=True)
r = subprocess.run([str(args.output / 'tick')], capture_output=True, text=True)
print(r.stdout, end='')
print(r.stderr, end='')
raise SystemExit(r.returncode)
