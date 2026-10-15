#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "KOReaderAutoSyncPolicy.h"
#include "KOReaderPullResume.h"

class GfxRenderer {
 public:
  void redriveNextRefresh() { ++redrives; }
  int redrives = 0;
};

struct TestSettings {
  struct {
    bool enabled = true;
  } ble;
};

TestSettings SETTINGS;

#define LOG_INF(...) ((void)0)

namespace koautosync {

using namespace kosyncauto;

enum class PullResult : uint8_t { Skipped, Kept, Applied, Failed };

bool pullRestartPending = false;
bool restoredFrame = false;
PullResume keptPull{};

namespace {
PullDebt fakeDebt = PullDebt::None;
Eligibility fakeHomeEligibility = Eligibility::Ready;
Eligibility fakeReaderEligibility = Eligibility::Ready;
PullResult fakePullResult = PullResult::Skipped;
int pullCalls = 0;
bool lastShowNotice = false;
std::string lastPullPath;

PullDebt pullDebt() { return fakeDebt; }
void setPullDebt(const PullDebt debt) { fakeDebt = debt; }
Eligibility gate(const bool onHome = false) { return onHome ? fakeHomeEligibility : fakeReaderEligibility; }
PullResult pullGated(GfxRenderer&, const std::string& path, const bool showNotice, const char*) {
  ++pullCalls;
  lastShowNotice = showNotice;
  lastPullPath = path;
  return fakePullResult;
}
}  // namespace

#include "KOReaderBeforeOpen.inc"

namespace {

std::string epubPathOfLength(const size_t length) {
  constexpr size_t suffixLength = sizeof(".epub") - 1;
  return "/" + std::string(length - suffixLength - 1, 'p') + ".epub";
}

class BeforeOpenBehaviorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fakeDebt = PullDebt::None;
    fakeHomeEligibility = Eligibility::Ready;
    fakeReaderEligibility = Eligibility::Ready;
    fakePullResult = PullResult::Skipped;
    pullCalls = 0;
    lastShowNotice = false;
    lastPullPath.clear();
    pullRestartPending = false;
    restoredFrame = false;
    keptPull = {};
    SETTINGS.ble.enabled = true;
  }

  GfxRenderer renderer;
};

TEST_F(BeforeOpenBehaviorTest, OwedOpenRestartsOnlyWhenThePathFitsTheRtcRecord) {
  fakeReaderEligibility = Eligibility::LowHeap;
  for (const size_t length : {size_t{175}, size_t{176}, size_t{255}}) {
    SetUp();
    fakeReaderEligibility = Eligibility::LowHeap;
    fakeDebt = PullDebt::Owed;
    const bool restart = beforeOpen(renderer, epubPathOfLength(length));

    EXPECT_EQ(restart, length == 175) << length;
    EXPECT_EQ(fakeDebt, length == 175 ? PullDebt::Armed : PullDebt::None) << length;
    EXPECT_EQ(pullCalls, 0) << length;
    EXPECT_EQ(std::strlen(keptPull.path), length == 175 ? length : 0u) << length;
  }
}

TEST_F(BeforeOpenBehaviorTest, OwedOpenPullsDirectlyWhenReaderHeapIsReady) {
  fakeDebt = PullDebt::Owed;
  fakeReaderEligibility = Eligibility::Ready;
  const std::string path = epubPathOfLength(175);

  EXPECT_FALSE(beforeOpen(renderer, path));
  EXPECT_EQ(fakeDebt, PullDebt::None);
  EXPECT_EQ(pullCalls, 1);
  EXPECT_TRUE(lastShowNotice);
  EXPECT_EQ(lastPullPath, path);
  EXPECT_EQ(keptPull.magic, 0u);
}

TEST_F(BeforeOpenBehaviorTest, OwedOpenPullsDirectlyWhenBleIsOff) {
  SETTINGS.ble.enabled = false;
  fakeDebt = PullDebt::Owed;
  fakeReaderEligibility = Eligibility::LowHeap;

  EXPECT_FALSE(beforeOpen(renderer, epubPathOfLength(175)));
  EXPECT_EQ(fakeDebt, PullDebt::None);
  EXPECT_EQ(pullCalls, 1);
  EXPECT_TRUE(lastShowNotice);
}

TEST_F(BeforeOpenBehaviorTest, SkippedPullConsumesArmedDebtForEveryPathLength) {
  for (const size_t length : {size_t{175}, size_t{176}, size_t{255}}) {
    SetUp();
    fakeDebt = PullDebt::Armed;
    fakePullResult = PullResult::Skipped;
    EXPECT_FALSE(beforeOpen(renderer, epubPathOfLength(length))) << length;
    EXPECT_EQ(fakeDebt, PullDebt::None) << length;
    EXPECT_EQ(pullCalls, 1) << length;
    EXPECT_FALSE(lastShowNotice) << length;
  }
}

TEST_F(BeforeOpenBehaviorTest, RestoredFrameIsRedrivenOnceBeforeTheOpenDecision) {
  restoredFrame = true;
  fakeDebt = PullDebt::None;
  EXPECT_FALSE(beforeOpen(renderer, epubPathOfLength(175)));
  EXPECT_EQ(renderer.redrives, 1);
  EXPECT_FALSE(restoredFrame);
}

}  // namespace
}  // namespace koautosync

#undef LOG_INF
