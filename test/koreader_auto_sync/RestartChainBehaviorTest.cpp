#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <utility>

#include "KOReaderAutoSyncPolicy.h"
#include "KOReaderPullResume.h"

class GfxRenderer {
 public:
  void redriveNextRefresh() { ++redrives; }
  int redrives = 0;
};

class MappedInputManager {};

int renderLockDepth = 0;
bool renderLockTrySucceeds = true;
int renderLockTryCalls = 0;
class RenderLock {
 public:
  struct TryTake {};
  RenderLock() : held(true) { ++renderLockDepth; }
  explicit RenderLock(TryTake) : held(renderLockTrySucceeds) {
    ++renderLockTryCalls;
    if (held) ++renderLockDepth;
  }
  ~RenderLock() {
    if (held) --renderLockDepth;
  }
  bool acquired() const { return held; }

 private:
  bool held;
};

int baseEnterCalls = 0;
class Activity {
 public:
  Activity(const char*, GfxRenderer&, MappedInputManager&) {}
  virtual ~Activity() = default;
  virtual void onEnter() { ++baseEnterCalls; }
};

struct TestSettings {
  struct {
    bool enabled = true;
  } ble;
};
TestSettings SETTINGS;

struct TestAppState {
  std::string openEpubPath;
};
TestAppState APP_STATE;

struct TestKOReaderStore {
  bool getAutoSync() const { return enabled; }
  bool hasCredentials() const { return credentials; }
  bool enabled = true;
  bool credentials = true;
};
TestKOReaderStore KOREADER_STORE;

struct TestActivityManager {
  void goToReader(const std::string& path) {
    ++readerCalls;
    readerPath = path;
  }
  void deferWrite(void (*write)()) {
    ++deferCalls;
    deferred = write;
  }
  int readerCalls = 0;
  std::string readerPath;
  int deferCalls = 0;
  void (*deferred)() = nullptr;
};
TestActivityManager activityManager;

int markSpentCalls = 0;
int silentRestartCalls = 0;
void markPageTurnerRestartSpent() { ++markSpentCalls; }
void silentRestartToReader() { ++silentRestartCalls; }

template <typename... Args>
void discardLog(const Args&...) {}
#define LOG_INF(...) discardLog(__VA_ARGS__)

namespace SleepActivity {
int notices = 0;
int noticeLockDepth = -1;
void showSyncing(GfxRenderer&) {
  ++notices;
  noticeLockDepth = renderLockDepth;
}
}  // namespace SleepActivity

uint32_t millis() {
  static uint32_t now = 0;
  return ++now;
}

namespace koautosync {

using namespace kosyncauto;

enum class PullResult : uint8_t { Skipped, Kept, Applied, Failed };

struct PullChange {
  std::string path;
  int fromSpineIndex = 0;
  int fromPage = 0;
  int toSpineIndex = 0;
  int toPage = 0;
  uint32_t fromOffset = 0;
  bool hasFromOffset = false;
};
struct PendingMark {
  std::string document;
  uint32_t token = 0;
  uint32_t place = 0;
};

namespace {
PullDebt fakeDebt = PullDebt::None;
Eligibility fakeHomeEligibility = Eligibility::Ready;
Eligibility fakeReaderEligibility = Eligibility::Ready;
PullResult fakePullResult = PullResult::Kept;
bool fakePrepareRestart = true;
int pullNowCalls = 0;
int prepareCalls = 0;
int gateLockDepth = -1;
int pullNowLockDepth = -1;

bool pullRestartPending = false;
bool restoredFrame = false;
bool usedPullNetwork = false;
bool hasPullChange = false;
bool hasPendingMark = false;
bool hasPendingHint = false;
bool markAfterFramePending = false;
PullResume keptPull{};
PullChange pullChange{};
PendingMark pendingMark{};
std::string pendingMarkPath;
int flushMarkCalls = 0;
bool flushMarkSucceeds = true;

PullDebt pullDebt() { return fakeDebt; }
void setPullDebt(const PullDebt debt) { fakeDebt = debt; }
Eligibility gate(const bool onHome = false) {
  gateLockDepth = renderLockDepth;
  return onHome ? fakeHomeEligibility : fakeReaderEligibility;
}
const char* why(Eligibility) { return "test"; }
PullResult pullNow(GfxRenderer&, const std::string&) {
  ++pullNowCalls;
  pullNowLockDepth = renderLockDepth;
  usedPullNetwork = true;
  return fakePullResult;
}
bool preparePageTurnerAfterPull(bool, bool, bool) {
  ++prepareCalls;
  return fakePrepareRestart;
}

#include "KOReaderPullGated.inc"
}  // namespace

void flushMark() {
  ++flushMarkCalls;
  if (flushMarkSucceeds) hasPendingMark = false;
}

void flushHint() { hasPendingHint = false; }

#include "KOReaderAfterFirstFrame.inc"

#include "KOReaderBeforeOpen.inc"

bool pullRestartReady() { return pullRestartPending; }
bool sleptLong() { return false; }

#include "KOReaderNoteStart.inc"

}  // namespace koautosync

#include "PullRestartActivity.inc"

namespace {

using namespace koautosync;
using namespace kosyncauto;

std::string epubPathOfLength(const size_t length) {
  constexpr size_t suffixLength = sizeof(".epub") - 1;
  return "/" + std::string(length - suffixLength - 1, 'p') + ".epub";
}

class RestartChainBehaviorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fakeDebt = PullDebt::None;
    fakeHomeEligibility = Eligibility::Ready;
    fakeReaderEligibility = Eligibility::Ready;
    fakePullResult = PullResult::Kept;
    fakePrepareRestart = true;
    pullNowCalls = 0;
    prepareCalls = 0;
    gateLockDepth = -1;
    pullNowLockDepth = -1;
    pullRestartPending = false;
    restoredFrame = false;
    usedPullNetwork = false;
    hasPullChange = false;
    hasPendingMark = false;
    hasPendingHint = false;
    markAfterFramePending = false;
    keptPull = {};
    pullChange = {};
    pendingMark = {};
    pendingMarkPath.clear();
    flushMarkCalls = 0;
    flushMarkSucceeds = true;
    SETTINGS.ble.enabled = true;
    APP_STATE.openEpubPath.clear();
    KOREADER_STORE = {};
    SleepActivity::notices = 0;
    SleepActivity::noticeLockDepth = -1;
    renderLockDepth = 0;
    renderLockTrySucceeds = true;
    renderLockTryCalls = 0;
    baseEnterCalls = 0;
    markSpentCalls = 0;
    silentRestartCalls = 0;
    activityManager = {};
  }

  GfxRenderer renderer;
  MappedInputManager input;
};

TEST_F(RestartChainBehaviorTest, ArmedHomeRestartDoesNotSpendTheBleRestartMemo) {
  fakeDebt = PullDebt::Owed;
  fakeReaderEligibility = Eligibility::LowHeap;
  const std::string path = epubPathOfLength(175);
  ASSERT_TRUE(beforeOpen(renderer, path));
  ASSERT_EQ(keptPull.magic, PULL_ARMED_MAGIC);
  ASSERT_FALSE(pullRestartPending);

  PullRestartActivity restart(renderer, input, path);
  restart.onEnter();

  EXPECT_EQ(markSpentCalls, 0);
  EXPECT_EQ(silentRestartCalls, 1);
  EXPECT_EQ(keptPull.magic, 0u);
  EXPECT_EQ(fakeDebt, PullDebt::Armed);
  EXPECT_EQ(APP_STATE.openEpubPath, path);
  EXPECT_EQ(activityManager.readerCalls, 1);
}

TEST_F(RestartChainBehaviorTest, PullGateLocksAfterEligibilityAndThroughThePull) {
  fakePullResult = PullResult::Skipped;
  fakePrepareRestart = false;
  EXPECT_EQ(pullGated(renderer, epubPathOfLength(175), true, "open pull"), PullResult::Skipped);
  EXPECT_EQ(gateLockDepth, 0);
  EXPECT_EQ(SleepActivity::noticeLockDepth, 1);
  EXPECT_EQ(pullNowLockDepth, 1);
  EXPECT_EQ(renderLockDepth, 0);
}

TEST_F(RestartChainBehaviorTest, DoneRestartIsPublishedBeforeTheActivitySpendsTheMemo) {
  for (const bool useStoredPath : {false, true}) {
    for (const size_t length : {size_t{175}, size_t{176}, size_t{255}}) {
      SetUp();
      const std::string path = epubPathOfLength(length);
      const bool canSave = useStoredPath || length == 175;
      APP_STATE.openEpubPath = path;
      hasPullChange = true;
      pullChange = {path, 1, 2, 3, 4, 5678, true};
      hasPendingMark = true;
      pendingMark = {std::string(32, 'h'), 17, 23};

      EXPECT_EQ(pullGated(renderer, path, false, "wake pull", useStoredPath), PullResult::Kept) << length;
      EXPECT_EQ(prepareCalls, canSave ? 1 : 0) << length;
      EXPECT_EQ(pullRestartPending, canSave) << length;
      EXPECT_EQ(keptPull.magic, canSave ? PULL_DONE_MAGIC : 0u) << length;
      if (canSave) {
        EXPECT_EQ(keptPull.useStoredPath, useStoredPath ? 1 : 0) << length;
        EXPECT_STREQ(keptPull.path, useStoredPath ? "/" : path.c_str()) << length;
      } else {
        EXPECT_EQ(markSpentCalls, 0) << length;
        EXPECT_EQ(silentRestartCalls, 0) << length;
        continue;
      }

      hasPullChange = false;
      pullChange = {};
      hasPendingMark = false;
      markAfterFramePending = false;
      pendingMark = {};
      pendingMarkPath.clear();
      if (!useStoredPath) APP_STATE.openEpubPath.clear();
      PullRestartActivity restart(renderer, input, path);
      restart.onEnter();
      EXPECT_EQ(markSpentCalls, 1) << length;
      EXPECT_EQ(fakeDebt, PullDebt::None) << length;
      EXPECT_EQ(APP_STATE.openEpubPath, path) << length;
      EXPECT_TRUE(hasPullChange) << length;
      EXPECT_EQ(pullChange.path, path) << length;
      EXPECT_EQ(pullChange.fromSpineIndex, 1) << length;
      EXPECT_EQ(pullChange.fromPage, 2) << length;
      EXPECT_EQ(pullChange.toSpineIndex, 3) << length;
      EXPECT_EQ(pullChange.toPage, 4) << length;
      EXPECT_EQ(pullChange.fromOffset, 5678u) << length;
      EXPECT_TRUE(pullChange.hasFromOffset) << length;
      EXPECT_TRUE(hasPendingMark) << length;
      EXPECT_TRUE(markAfterFramePending) << length;
      EXPECT_TRUE(pendingMark.document.empty()) << length;
      EXPECT_EQ(pendingMark.token, 17u) << length;
      EXPECT_EQ(pendingMark.place, 23u) << length;
      EXPECT_EQ(pendingMarkPath, path) << length;
      EXPECT_EQ(activityManager.deferCalls, 0) << length;
      EXPECT_EQ(takePullMagic(keptPull, true), 0u) << length;
    }
  }
}

TEST_F(RestartChainBehaviorTest, FailedNetworkPullStillPublishesRestartBeforeTheFirstFrame) {
  fakePullResult = PullResult::Failed;
  fakePrepareRestart = true;
  const std::string path = epubPathOfLength(175);

  EXPECT_EQ(pullGated(renderer, path, false, "wake pull"), PullResult::Failed);
  EXPECT_EQ(pullNowCalls, 1);
  EXPECT_EQ(prepareCalls, 1);
  EXPECT_TRUE(pullRestartPending);
  EXPECT_EQ(keptPull.magic, PULL_DONE_MAGIC);

  PullRestartActivity restart(renderer, input, path);
  restart.onEnter();
  EXPECT_EQ(markSpentCalls, 1);
  EXPECT_EQ(silentRestartCalls, 1);
}

TEST_F(RestartChainBehaviorTest, FirstFrameMarkFlushKeepsPendingWorkWhenTheLockOrCardIsUnavailable) {
  afterFirstFrame();
  EXPECT_EQ(renderLockTryCalls, 0);
  EXPECT_EQ(flushMarkCalls, 0);

  hasPendingMark = true;
  markAfterFramePending = true;
  renderLockTrySucceeds = false;
  afterFirstFrame();
  EXPECT_EQ(renderLockTryCalls, 1);
  EXPECT_EQ(flushMarkCalls, 0);
  EXPECT_TRUE(hasPendingMark);
  EXPECT_TRUE(markAfterFramePending);

  renderLockTrySucceeds = true;
  flushMarkSucceeds = false;
  afterFirstFrame();
  EXPECT_EQ(renderLockTryCalls, 2);
  EXPECT_EQ(flushMarkCalls, 1);
  EXPECT_TRUE(hasPendingMark);
  EXPECT_FALSE(markAfterFramePending);

  afterFirstFrame();
  EXPECT_EQ(renderLockTryCalls, 2);
  EXPECT_EQ(flushMarkCalls, 1);
  EXPECT_TRUE(hasPendingMark);
  EXPECT_EQ(renderLockDepth, 0);
}

TEST_F(RestartChainBehaviorTest, FirstFrameFlushesAQueuedHintWithoutAQueuedMark) {
  hasPendingHint = true;
  afterFirstFrame();
  EXPECT_EQ(renderLockTryCalls, 1);
  EXPECT_FALSE(hasPendingHint);
  EXPECT_EQ(renderLockDepth, 0);

  hasPendingHint = true;
  renderLockTrySucceeds = false;
  afterFirstFrame();
  EXPECT_TRUE(hasPendingHint);
  EXPECT_EQ(renderLockTryCalls, 2);
}

}  // namespace

#undef LOG_INF
