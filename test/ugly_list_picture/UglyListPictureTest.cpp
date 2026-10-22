// The tenor/ugly list paints a row's label picture (a font's name in its own letters) by hand where the layout
// stood it, with the circle of the selected row round the picture, and keeps the words of rows without one.
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "MappedInputManager.h"
#include "activities/UiListActivity.h"
#include "util/ButtonNavigator.h"

HalDisplay& hostTestDisplay();
namespace faketest { void reset(); }
namespace hosttest {
extern bool ugly;
extern int pixels, minX, minY, maxX, maxY;
extern std::vector<freeink::ui::Rect> rings, around;
extern std::vector<std::string> words;
}  // namespace hosttest

namespace fui = freeink::ui;

namespace {
// 8 columns of ink per row: a picture whose every bit is ink, so each drawn pixel is countable.
const uint8_t kInk[40 * 4] = {
#define ROW 0xff, 0xff, 0xff, 0xff
    ROW, ROW, ROW, ROW, ROW, ROW, ROW, ROW, ROW, ROW, ROW, ROW, ROW, ROW, ROW, ROW, ROW, ROW, ROW, ROW,
    ROW, ROW, ROW, ROW, ROW, ROW, ROW, ROW, ROW, ROW, ROW, ROW, ROW, ROW, ROW, ROW, ROW, ROW, ROW, ROW};
#undef ROW
const fui::LabelImage kPicture{{kInk, 32, 20, fui::BitmapFormat::BW1, false}, 16};

class PictureList final : public UiListActivity {
 public:
  PictureList(GfxRenderer& r, MappedInputManager& i) : UiListActivity("PictureList", r, i) {}
  fui::ListItem items[3];
  int listCount() const override { return 3; }
  void activateIndex(int) override {}
  void buildScreen(UiScreen& screen) override {
    fui::ListProps props;
    props.items = items;
    props.count = 3;
    props.action = ACTION_ROW;
    props.labelAscent = 16;
    syncListViewport(screen, props);
    screen.list(props);
  }
  void paint() {
    RenderLock lock;
    render(std::move(lock));
  }
  int selected() { return activeNav().selected; }
  void select(int row) { activeNav().selected = row; }
};

struct Fixture : ::testing::Test {
  HalGPIO gpio;
  GfxRenderer renderer{hostTestDisplay()};
  MappedInputManager input{gpio, renderer};
  PictureList list{renderer, input};
  void SetUp() override {
    ButtonNavigator::setMappedInputManager(input);
    faketest::reset();
    hosttest::pixels = 0;
    hosttest::minX = hosttest::minY = 1 << 30;
    hosttest::maxX = hosttest::maxY = -1;
    hosttest::rings.clear();
    hosttest::around.clear();
    hosttest::words.clear();
    list.items[0].labelImage = &kPicture;
    list.items[0].value = "Installed";
    list.items[1].label = "Bookerly";  // a row of the reader's own: words
    list.items[2].labelImage = &kPicture;
    list.onEnter();
  }
  void TearDown() override { hosttest::ugly = false; }
};

TEST_F(Fixture, UglyListPaintsTheLabelPictureWhereTheLayoutStoodIt) {
  hosttest::ugly = true;
  list.select(0);
  list.paint();
  // Two pictures of 32 x 20 ink pixels each reach the page (the hand draws nothing else here: its words are stubbed).
  EXPECT_EQ(hosttest::pixels, 2 * 32 * 20);
  EXPECT_EQ(hosttest::words, std::vector<std::string>({"Installed", "Bookerly"}));  // no words for a picture row
}

TEST_F(Fixture, UglyCircleOfTheSelectedRowGoesRoundThePicture) {
  hosttest::ugly = true;
  list.select(0);
  list.paint();
  ASSERT_FALSE(hosttest::around.empty());  // marks() of the selected row
  const auto circle = hosttest::around[0];
  EXPECT_EQ(circle.width, 32);  // the picture's own box, as drawn
  EXPECT_EQ(circle.height, 20);
  // The picture's ink is inside it.
  EXPECT_GE(hosttest::minX, circle.x);
  EXPECT_LE(hosttest::maxX, circle.right() - 1);
}

TEST_F(Fixture, UglyCircleFindsAPictureWhenTheRowHasOtherWordsToo) {
  hosttest::ugly = true;
  list.items[0].subtitle = "In use";  // a second line: the picture is still the label the circle goes round
  list.select(0);
  list.paint();
  ASSERT_FALSE(hosttest::around.empty());
  EXPECT_EQ(hosttest::around[0].width, 32);
}

TEST_F(Fixture, CrossListKeepsItsOwnPaintAndTheUglyHandStaysOut) {
  hosttest::ugly = false;
  list.paint();
  EXPECT_TRUE(hosttest::rings.empty());
  EXPECT_TRUE(hosttest::words.empty());
}
}  // namespace
