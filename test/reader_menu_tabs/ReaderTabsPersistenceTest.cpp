#include <gtest/gtest.h>

#include "MenuCustomization.cpp"

namespace {
constexpr const char* MENU_PATH = "/.crosspoint/menu-customization.json";

class ReaderTabsPersistence : public testing::Test {
 protected:
  void SetUp() override {
    Storage.files.clear();
    Storage.writes = true;
    restart();
  }

  void restart() {
    menucustom::data = menucustom::State{};
    menucustom::loaded = false;
  }
};
}

TEST_F(ReaderTabsPersistence, LegacyOrderAppendsTextTabAndRetainsPins) {
  Storage.files[MENU_PATH] = R"({"version":1,"tabs":{"reader":[3,1,0,2]},"pins":["settings/sleepTimeoutMinutes"]})";
  menucustom::load();
  for (int slot = 0; slot < 5; ++slot) {
    EXPECT_EQ(menucustom::idAt(2, slot, 5), (std::array<int, 5>{3, 1, 0, 2, 4})[slot]);
  }
  EXPECT_EQ(menucustom::adjacent(2, 2, 5, 1), 4);
  EXPECT_EQ(menucustom::adjacent(2, 4, 5, 1), 3);
  EXPECT_EQ(menucustom::state().pinCount, 1);
}

TEST_F(ReaderTabsPersistence, TextTabReorderSurvivesRestartAndFourTabShellSkipsIt) {
  menucustom::load();
  ASSERT_TRUE(menucustom::moveTab(2, 4, 5, -1));
  JsonDocument saved;
  ASSERT_FALSE(deserializeJson(saved, Storage.files[MENU_PATH]));
  EXPECT_EQ(saved["tabs"]["reader"].size(), 5);
  restart();
  menucustom::load();
  for (int slot = 0; slot < 5; ++slot) {
    EXPECT_EQ(menucustom::idAt(2, slot, 5), (std::array<int, 5>{0, 1, 2, 4, 3})[slot]);
  }
  for (int slot = 0; slot < 4; ++slot) {
    EXPECT_EQ(menucustom::idAt(2, slot, 4), slot);
  }
  EXPECT_EQ(menucustom::adjacent(2, 2, 4, 1), 3);
}
