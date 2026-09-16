#include <CrossPointSettings.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <MappedInputManager.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

HalDisplay& hostTestDisplay();

namespace faketest {
extern bool pressed[8];
extern bool released[8];
extern bool held[8];
extern bool touch;
void reset();
}  // namespace faketest

namespace {

using Button = MappedInputManager::Button;

constexpr Button kButtons[] = {Button::Back,       Button::Confirm,  Button::Left,        Button::Right,
                               Button::Up,         Button::Down,     Button::Power,       Button::PageBack,
                               Button::PageForward, Button::NavNext, Button::NavPrevious, Button::ScreenLeft,
                               Button::ScreenRight, Button::ScreenUp, Button::ScreenDown};

const char* kButtonNames[] = {"Back",       "Confirm",  "Left",        "Right",      "Up",
                              "Down",       "Power",    "PageBack",    "PageForward", "NavNext",
                              "NavPrevious", "ScreenLeft", "ScreenRight", "ScreenUp",  "ScreenDown"};

const char* kHwNames[] = {"BACK", "CONFIRM", "LEFT", "RIGHT", "UP", "DOWN", "POWER"};

const char* orientationName(GfxRenderer::Orientation o) {
  switch (o) {
    case GfxRenderer::Portrait: return "Portrait";
    case GfxRenderer::LandscapeClockwise: return "LandscapeCW";
    case GfxRenderer::PortraitInverted: return "PortraitInverted";
    case GfxRenderer::LandscapeCounterClockwise: return "LandscapeCCW";
  }
  return "?";
}

// Sinh bang: voi moi cau hinh, moi nut phan cung, liet ke cac nut lo gic bao "vua nhan".
std::string buildMappingTable() {
  std::ostringstream out;
  const GfxRenderer::Orientation orientations[] = {GfxRenderer::Portrait, GfxRenderer::LandscapeClockwise,
                                                   GfxRenderer::PortraitInverted,
                                                   GfxRenderer::LandscapeCounterClockwise};
  for (const bool followOrientation : {false, true}) {
    for (int side = 0; side < CrossPointSettings::SIDE_BUTTON_LAYOUT_COUNT; ++side) {
      for (const auto orientation : orientations) {
        HalGPIO gpio;
        // GfxRenderer binds the display by reference and never touches it here;
        // FakeUi.cpp provides the storage without constructing the panel driver.
        GfxRenderer renderer(hostTestDisplay());
        renderer.setOrientation(orientation);
        MappedInputManager input(gpio, renderer);

        SETTINGS.frontButtonFollowOrientation = followOrientation;
        SETTINGS.sideButtonLayout = static_cast<CrossPointSettings::SIDE_BUTTON_LAYOUT>(side);
        SETTINGS.frontButtonBack = CrossPointSettings::FRONT_HW_BACK;
        SETTINGS.frontButtonConfirm = CrossPointSettings::FRONT_HW_CONFIRM;
        SETTINGS.frontButtonLeft = CrossPointSettings::FRONT_HW_LEFT;
        SETTINGS.frontButtonRight = CrossPointSettings::FRONT_HW_RIGHT;

        out << "follow=" << (followOrientation ? 1 : 0) << " side=" << side
            << " orientation=" << orientationName(orientation) << "\n";
        for (int hw = 0; hw <= HalGPIO::BTN_POWER; ++hw) {
          faketest::reset();
          faketest::pressed[hw] = true;
          out << "  hw " << kHwNames[hw] << " ->";
          bool any = false;
          for (size_t b = 0; b < sizeof(kButtons) / sizeof(kButtons[0]); ++b) {
            if (input.wasPressed(kButtons[b])) {
              out << " " << kButtonNames[b];
              any = true;
            }
          }
          if (!any) out << " (khong nut lo gic nao)";
          out << "\n";
        }
      }
    }
  }
  return out.str();
}

TEST(MappedInputSafetyNet, NhanPhimKhopVoiViecNutDoLam) {
  const struct {
    uint8_t back, confirm, left, right;
  } layouts[] = {
      {CrossPointSettings::FRONT_HW_BACK, CrossPointSettings::FRONT_HW_CONFIRM, CrossPointSettings::FRONT_HW_LEFT,
       CrossPointSettings::FRONT_HW_RIGHT},
      // Doi cho Back va Confirm: nguoi dung gan lai duoc, nen nhan phai di theo.
      {CrossPointSettings::FRONT_HW_CONFIRM, CrossPointSettings::FRONT_HW_BACK, CrossPointSettings::FRONT_HW_LEFT,
       CrossPointSettings::FRONT_HW_RIGHT},
      // Doi cho hai nut dieu huong.
      {CrossPointSettings::FRONT_HW_BACK, CrossPointSettings::FRONT_HW_CONFIRM, CrossPointSettings::FRONT_HW_RIGHT,
       CrossPointSettings::FRONT_HW_LEFT},
  };

  for (const auto& layout : layouts) {
    HalGPIO gpio;
    GfxRenderer renderer(hostTestDisplay());
    MappedInputManager input(gpio, renderer);
    SETTINGS.frontButtonBack = layout.back;
    SETTINGS.frontButtonConfirm = layout.confirm;
    SETTINGS.frontButtonLeft = layout.left;
    SETTINGS.frontButtonRight = layout.right;

    const auto labels = input.mapLabels("QUAYLAI", "CHON", "TRUOC", "SAU");
    const char* atHardware[] = {labels.btn1, labels.btn2, labels.btn3, labels.btn4};
    const uint8_t hardware[] = {HalGPIO::BTN_BACK, HalGPIO::BTN_CONFIRM, HalGPIO::BTN_LEFT, HalGPIO::BTN_RIGHT};

    for (size_t i = 0; i < 4; ++i) {
      faketest::reset();
      faketest::pressed[hardware[i]] = true;

      std::string expected;
      if (input.wasPressed(MappedInputManager::Button::Back)) expected = "QUAYLAI";
      if (input.wasPressed(MappedInputManager::Button::Confirm)) expected = "CHON";
      if (input.wasPressed(MappedInputManager::Button::Left)) expected = "TRUOC";
      if (input.wasPressed(MappedInputManager::Button::Right)) expected = "SAU";

      ASSERT_FALSE(expected.empty()) << "nut phan cung " << i << " khong chay nut lo gic nao";
      EXPECT_EQ(std::string(atHardware[i]), expected)
          << "nhan duoi nut phan cung " << i << " noi mot dang, nut do lam mot neo";
    }
  }
  faketest::reset();
}

TEST(MappedInputSafetyNet, AnhXaNutGiuNguyenNhuHomNay) {
  const std::string actual = buildMappingTable();
  const std::string goldenPath = std::string(MAPPED_INPUT_GOLDEN_DIR) + "/golden_mapping.txt";

  if (std::getenv("REGEN_GOLDEN") != nullptr) {
    std::ofstream(goldenPath) << actual;
    GTEST_SKIP() << "Da ghi lai ban vang vao " << goldenPath;
  }

  std::ifstream in(goldenPath);
  ASSERT_TRUE(in.good()) << "Thieu ban vang " << goldenPath << ". Chay lai voi REGEN_GOLDEN=1.";
  std::stringstream buf;
  buf << in.rdbuf();
  EXPECT_EQ(buf.str(), actual);
}

}  // namespace
