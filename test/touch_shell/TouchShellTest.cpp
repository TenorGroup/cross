#include <gtest/gtest.h>

#include "TenorMenuChrome.h"

HalGPIO gpio;

namespace shell {
bool ugly = false;
bool uglyParts() { return ugly; }
}

#include "X3ScreenLayoutPolicy.inc"

class TouchShell : public ::testing::Test {
 protected:
  void SetUp() override {
    gpio.touch = false;
    shell::ugly = false;
    SETTINGS.tipsEnabled = 1;
  }
};

namespace {
constexpr const char* kPinHint = "Gi\xE1\xBB\xAF \xEE\x84\x80: Ghim";            // names Select
constexpr const char* kSideHint = "Gi\xE1\xBB\xAF \xEE\x84\x87: \xC4\x90\xE1\xBB\x8D" "c";  // names a side key
constexpr const char* kPinnedRow = "\xEE\x84\x8A Hi\xE1\xBB\x83n th\xE1\xBB\x8B";  // the pin mark, not a key
constexpr const char* kPlain = "Kh\xC3\xB4ng t\xC3\xACm th\xE1\xBA\xA5y s\xC3\xA1" "ch";
}  // namespace

#if FREEINK_DEVICE_X4PRO
TEST_F(TouchShell, X4ProDrawsTenorCrossWithTheTouchPanelUp) {
  gpio.touch = true;
  EXPECT_TRUE(tenorchrome::kTouchShell);
  EXPECT_TRUE(tenorchrome::enabled());
}

TEST_F(TouchShell, X4ProStillDrawsTenorCrossWhenTheTouchPanelIsMissing) {
  gpio.touch = false;
  EXPECT_TRUE(tenorchrome::enabled());
}

TEST_F(TouchShell, X4ProHidesTipsThatNameAButton) {
  EXPECT_FALSE(tenorchrome::tipShown(kPinHint));
  EXPECT_FALSE(tenorchrome::tipShown(kSideHint));
  EXPECT_TRUE(tenorchrome::tipShown(kPinnedRow));
  EXPECT_TRUE(tenorchrome::tipShown(kPlain));
  EXPECT_TRUE(tenorchrome::tipShown(""));
}

TEST_F(TouchShell, X4ProNeverEnablesX3LayoutOrUsesItsTipsSwitch) {
  SETTINGS.tipsEnabled = 0;
  for (const bool touch : {false, true}) {
    gpio.touch = touch;
    for (const bool ugly : {false, true}) {
      shell::ugly = ugly;
      EXPECT_FALSE(tenorchrome::x3ScreenLayoutEnabled());
      EXPECT_FALSE(tenorchrome::tipShown(kPinHint));
      EXPECT_TRUE(tenorchrome::tipShown(kPlain));
    }
  }
}
#else
TEST_F(TouchShell, ButtonReadersAskThePanelAsBefore) {
  EXPECT_FALSE(tenorchrome::kTouchShell);
  gpio.touch = false;
  EXPECT_TRUE(tenorchrome::enabled());
  gpio.touch = true;
  EXPECT_FALSE(tenorchrome::enabled());
}

TEST_F(TouchShell, ButtonReadersKeepEveryTip) {
  EXPECT_TRUE(tenorchrome::tipShown(kPinHint));
  EXPECT_TRUE(tenorchrome::tipShown(kSideHint));
  EXPECT_TRUE(tenorchrome::tipShown(kPlain));
}

TEST_F(TouchShell, X3LayoutNeedsTheButtonShell) {
  for (const bool touch : {false, true}) {
    gpio.touch = touch;
    for (const bool ugly : {false, true}) {
      shell::ugly = ugly;
      EXPECT_EQ(tenorchrome::x3ScreenLayoutEnabled(), !touch);
    }
  }
}

TEST_F(TouchShell, TipsSwitchOnlyAppliesToTheX3Layout) {
  SETTINGS.tipsEnabled = 0;
  EXPECT_FALSE(tenorchrome::tipShown(kPinHint));
  EXPECT_FALSE(tenorchrome::tipShown(kPlain));
  shell::ugly = true;
  EXPECT_FALSE(tenorchrome::tipShown(kPlain));
  SETTINGS.tipsEnabled = 1;
  EXPECT_TRUE(tenorchrome::tipShown(kPlain));
  SETTINGS.tipsEnabled = 0;
  shell::ugly = false;
  gpio.touch = true;
  EXPECT_TRUE(tenorchrome::tipShown(kPlain));
}
#endif
