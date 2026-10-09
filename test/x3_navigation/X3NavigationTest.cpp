#include <gtest/gtest.h>

#include "src/components/X3NavigationPath.h"

struct PathRenderer {
  int getTextWidth(int, const char* text) const {
    int width = 0;
    for (const unsigned char* cursor = reinterpret_cast<const unsigned char*>(text); *cursor; ++cursor)
      if ((*cursor & 0xc0) != 0x80) width += 10;
    return width;
  }
};

TEST(X3Navigation, SettingsDeviceKeyboardHasBothAncestors) {
  PathRenderer renderer;
  const auto path = tenorchrome::layoutX3Navigation(renderer, 1,
      {{tenorchrome::X3PathIcon::Settings, ""}, {tenorchrome::X3PathIcon::Device, "Thiết bị"},
       {tenorchrome::X3PathIcon::Keyboard, "Tên thiết bị"}}, 496, 48);
  ASSERT_EQ(path.size(), 3u);
  EXPECT_EQ(path[0].icon, tenorchrome::X3PathIcon::Settings);
  EXPECT_EQ(path[1].icon, tenorchrome::X3PathIcon::Device);
  EXPECT_EQ(path.back().text, "Tên thiết bị");
  EXPECT_TRUE(path.back().current);
  EXPECT_EQ(path.back().width, renderer.getTextWidth(1, "Tên thiết bị") + 28);
}

TEST(X3Navigation, MiddleParentsCollapseBeforeCurrentTitle) {
  PathRenderer renderer;
  const auto path = tenorchrome::layoutX3Navigation(renderer, 1,
      {{tenorchrome::X3PathIcon::Settings, ""}, {tenorchrome::X3PathIcon::Device, "1"},
       {tenorchrome::X3PathIcon::Display, "2"}, {tenorchrome::X3PathIcon::Keyboard, "3"},
       {tenorchrome::X3PathIcon::More, "A very long current screen title"}}, 340, 60);
  ASSERT_EQ(path.size(), 4u);
  EXPECT_EQ(path[0].icon, tenorchrome::X3PathIcon::Settings);
  EXPECT_EQ(path[1].text, "…");
  EXPECT_EQ(path[2].icon, tenorchrome::X3PathIcon::Keyboard);
  EXPECT_EQ(path[3].text.substr(path[3].text.size() - 3), "…");
  EXPECT_EQ(path[3].text.front(), 'A');
  int width = 0;
  for (const auto& pill : path) width += pill.width;
  EXPECT_LE(width + 8 * static_cast<int>(path.size() - 1), 340);
}

TEST(X3Navigation, CommonGeometryAcrossAllSizes) {
  PathRenderer renderer;
  for (int height : {48, 55, 60}) {
    const auto path = tenorchrome::layoutX3Navigation(renderer, 1,
        {{tenorchrome::X3PathIcon::File, ""}, {tenorchrome::X3PathIcon::More, "Short"}}, 496, height);
    ASSERT_EQ(path.size(), 2u);
    EXPECT_EQ(path[0].width, height);
    EXPECT_EQ(path[1].width, 78);
    EXPECT_FALSE(path[0].current);
    EXPECT_TRUE(path[1].current);
  }
}
