#include <gtest/gtest.h>

#include <fstream>
#include <string>

#include "BleKeyboardHost.h"
#include "BlePageTurnerRuntime.h"
#include "FileTransferState.h"
#include "FontCacheManager.h"
#include "GfxRenderer.h"
#include "HalMemory.h"
#include "Logging.h"
#include "activities/RenderLock.h"

namespace {

constexpr size_t kEnoughFree = 65536;
constexpr size_t kEnoughLargest = 32768;

class BlePageTurnerRuntimeTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto& host = freeink::BleKeyboardHost::getInstance();
    host.reset();
    HalMemory::internalHeap = {kEnoughFree, 249216, 0, kEnoughLargest};
    HalMemory::internalHeapReads = 0;
    ble_runtime_test::cacheReleaseCalls = 0;
    ble_runtime_test::cacheReleaseHeldLock = false;
    ble_runtime_test::renderLockHeld = false;
    ble_runtime_test::renderLockAcquires = 0;
    ble_runtime_test::clearLogs();
    renderer.setFontCacheManager(&fontCacheManager);
    ASSERT_FALSE(filetransfer::isActive());
  }

  void TearDown() override {
    while (filetransfer::isActive()) filetransfer::release();
    freeink::BleKeyboardHost::getInstance().reset();
  }

  GfxRenderer renderer;
  FontCacheManager fontCacheManager;
};

TEST_F(BlePageTurnerRuntimeTest, LowMemorySkipsHostWithoutInitialization) {
  HalMemory::internalHeap.freeBytes = kEnoughFree - 1;

  EXPECT_FALSE(freeink::ble::begin(renderer));
  EXPECT_EQ(freeink::BleKeyboardHost::getInstance().beginCalls, 0u);
  EXPECT_EQ(ble_runtime_test::cacheReleaseCalls, 1u);
  EXPECT_TRUE(ble_runtime_test::cacheReleaseHeldLock);
  ASSERT_EQ(ble_runtime_test::logs.size(), 1u);
  EXPECT_NE(ble_runtime_test::logs.front().find("free=65535"), std::string::npos);
  EXPECT_NE(ble_runtime_test::logs.front().find("required_free=65536"), std::string::npos);
}

TEST_F(BlePageTurnerRuntimeTest, FragmentedLargestBlockSkipsHostWithoutInitialization) {
  HalMemory::internalHeap.largestBlockBytes = kEnoughLargest - 1;

  EXPECT_FALSE(freeink::ble::begin(renderer));
  EXPECT_EQ(freeink::BleKeyboardHost::getInstance().beginCalls, 0u);
  EXPECT_EQ(ble_runtime_test::cacheReleaseCalls, 1u);
  EXPECT_TRUE(ble_runtime_test::cacheReleaseHeldLock);
  ASSERT_EQ(ble_runtime_test::logs.size(), 1u);
  EXPECT_NE(ble_runtime_test::logs.front().find("largest=32767"), std::string::npos);
}

TEST_F(BlePageTurnerRuntimeTest, BusyTransferSkipsBeforeCacheReleaseAndHeapProbe) {
  filetransfer::acquire();
  HalMemory::internalHeap = {123, 456, 0, 789};

  EXPECT_FALSE(freeink::ble::begin(renderer));
  EXPECT_EQ(freeink::BleKeyboardHost::getInstance().beginCalls, 0u);
  EXPECT_EQ(ble_runtime_test::cacheReleaseCalls, 0u);
  EXPECT_EQ(HalMemory::internalHeapReads, 1u);
  ASSERT_EQ(ble_runtime_test::logs.size(), 1u);
  EXPECT_NE(ble_runtime_test::logs.front().find("filetransfer-active"), std::string::npos);
  EXPECT_NE(ble_runtime_test::logs.front().find("free=123"), std::string::npos);
}

TEST_F(BlePageTurnerRuntimeTest, AcquirePropagatesPendingBleTeardownFailure) {
  auto& host = freeink::BleKeyboardHost::getInstance();
  host.pendingCleanup = true;
  host.endResult = false;

  EXPECT_FALSE(host.isRunning());
  EXPECT_FALSE(filetransfer::acquire());
  EXPECT_EQ(host.endCalls, 1u);
  EXPECT_EQ(host.lastEndTimeoutMs, 1000u);
  EXPECT_TRUE(filetransfer::isActive());

  filetransfer::release();
  EXPECT_FALSE(filetransfer::isActive());
}

TEST_F(BlePageTurnerRuntimeTest, PropagatesHostInitializationFailure) {
  freeink::BleKeyboardHost::getInstance().beginResult = false;

  EXPECT_FALSE(freeink::ble::begin(renderer));
  EXPECT_EQ(freeink::BleKeyboardHost::getInstance().beginCalls, 1u);
  EXPECT_EQ(ble_runtime_test::cacheReleaseCalls, 1u);
  EXPECT_TRUE(ble_runtime_test::cacheReleaseHeldLock);
  EXPECT_FALSE(freeink::BleKeyboardHost::getInstance().beginHeldRenderLock);
  EXPECT_FALSE(ble_runtime_test::renderLockHeld);
}

TEST_F(BlePageTurnerRuntimeTest, PendingTeardownCannotRestartOrReleaseCaches) {
  auto& host = freeink::BleKeyboardHost::getInstance();
  host.stopping = true;
  EXPECT_FALSE(freeink::ble::begin(renderer));
  EXPECT_EQ(host.beginCalls, 0u);
  EXPECT_EQ(ble_runtime_test::cacheReleaseCalls, 0u);
  EXPECT_EQ(HalMemory::internalHeapReads, 0u);
}

TEST_F(BlePageTurnerRuntimeTest, SuccessfulStackWithoutReaderHeadroomIsRolledBackOutsideRenderLock) {
  auto& host = freeink::BleKeyboardHost::getInstance();
  host.changeHeapOnBegin = true;
  host.heapAfterBegin = {50000, 249216, 0, kEnoughLargest - 1};

  EXPECT_FALSE(freeink::ble::begin(renderer));
  EXPECT_EQ(host.beginCalls, 1u);
  EXPECT_EQ(host.endCalls, 1u);
  EXPECT_FALSE(host.beginHeldRenderLock);
  EXPECT_FALSE(host.endHeldRenderLock);
  EXPECT_FALSE(host.isRunning());
  ASSERT_EQ(ble_runtime_test::logs.size(), 1u);
  EXPECT_NE(ble_runtime_test::logs.front().find("post-init-headroom"), std::string::npos);
}

TEST_F(BlePageTurnerRuntimeTest, ReaderHeadroomRollbackCanRemainPendingWithoutRestarting) {
  auto& host = freeink::BleKeyboardHost::getInstance();
  host.changeHeapOnBegin = true;
  host.heapAfterBegin = {50000, 249216, 0, kEnoughLargest - 1};
  host.endResult = false;

  EXPECT_FALSE(freeink::ble::begin(renderer));
  EXPECT_TRUE(host.isStopping());
  EXPECT_FALSE(freeink::ble::begin(renderer));
  EXPECT_EQ(host.beginCalls, 1u);
  EXPECT_EQ(host.endCalls, 1u);
}

TEST_F(BlePageTurnerRuntimeTest, ExactReaderBlockAfterStackStartIsAccepted) {
  auto& host = freeink::BleKeyboardHost::getInstance();
  host.changeHeapOnBegin = true;
  host.heapAfterBegin = {40000, 249216, 0, kEnoughLargest};

  EXPECT_TRUE(freeink::ble::begin(renderer));
  EXPECT_TRUE(host.isRunning());
  EXPECT_EQ(host.endCalls, 0u);
}

TEST_F(BlePageTurnerRuntimeTest, RunningHostReturnsTrueWithoutRetryOrCacheChurn) {
  ASSERT_TRUE(freeink::ble::begin(renderer));
  ASSERT_EQ(freeink::BleKeyboardHost::getInstance().beginCalls, 1u);
  ASSERT_EQ(ble_runtime_test::cacheReleaseCalls, 1u);
  const unsigned readsAfterFirstBegin = HalMemory::internalHeapReads;

  EXPECT_TRUE(freeink::ble::begin(renderer));
  EXPECT_EQ(freeink::BleKeyboardHost::getInstance().beginCalls, 1u);
  EXPECT_EQ(ble_runtime_test::cacheReleaseCalls, 1u);
  EXPECT_EQ(HalMemory::internalHeapReads, readsAfterFirstBegin);
}

TEST_F(BlePageTurnerRuntimeTest, ActivityTransitionDefersUntilRadioMemoryIsReleased) {
  auto& host = freeink::BleKeyboardHost::getInstance();
  host.running = true;
  host.endResult = false;
  EXPECT_FALSE(freeink::ble::suspendForTransition());
  EXPECT_EQ(host.lastEndTimeoutMs, 0u);
  EXPECT_FALSE(host.endHeldRenderLock);
  EXPECT_TRUE(host.isStopping());

  EXPECT_FALSE(freeink::ble::suspendForTransition());
  host.endResult = true;
  EXPECT_TRUE(freeink::ble::suspendForTransition());
  EXPECT_FALSE(host.isStopping());
  EXPECT_EQ(host.endCalls, 3u);
}

TEST_F(BlePageTurnerRuntimeTest, ActivityTransitionWithIdleRadioDoesNotAllocateOrStopAgain) {
  EXPECT_TRUE(freeink::ble::suspendForTransition());
  EXPECT_EQ(freeink::BleKeyboardHost::getInstance().endCalls, 0u);
  EXPECT_EQ(ble_runtime_test::renderLockAcquires, 0u);
  EXPECT_EQ(HalMemory::internalHeapReads, 0u);
}

TEST(BlePageTurnerRuntimeSourceContractTest, AllAppBeginCallsUseTheRuntimeFunnel) {
  const std::string root = REPO_ROOT_PATH;
  auto read = [](const std::string& path) {
    std::ifstream stream(path);
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
  };
  const std::string main = read(root + "/src/main.cpp");
  const std::string settings = read(root + "/src/activities/settings/BlePageTurnerActivity.cpp");

  EXPECT_EQ(main.find("bleHid.begin(\"FreeInk\")"), std::string::npos);
  EXPECT_EQ(main.find("BleKeyboardHost::getInstance().begin(\"FreeInk\")"), std::string::npos);
  EXPECT_EQ(settings.find("BleHid.begin(\"FreeInk\")"), std::string::npos);
  EXPECT_NE(main.find("freeink::ble::begin(renderer)"), std::string::npos);
  EXPECT_NE(settings.find("backend::begin(renderer)"), std::string::npos);
}

}  // namespace
