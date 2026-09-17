#include <gtest/gtest.h>

#include "BlePageTurnerRuntime.h"
#include "GfxRenderer.h"

TEST(BlePageTurnerRuntimeHardwareOffTest, BeginReturnsFalseWithoutBleCapability) {
  GfxRenderer renderer;
  EXPECT_FALSE(freeink::ble::begin(renderer));
}
