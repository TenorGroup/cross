#include <gtest/gtest.h>

#include "src/shells/UglyFont.h"

namespace {
struct Reader {
  char name[64] = "Bokerlam";
  bool builtinDefault = true;
  uint8_t memo = 0;
  void enter(bool onCard) { uglyfont::take(name, builtinDefault, memo, onCard); }
  void leave() { uglyfont::give(name, memo); }
  void pick(const char* family) {  // the user picks a font by hand ("" is a built-in one)
    std::strcpy(name, family);
    uglyfont::pickedByHand(memo);
  }
};
}  // namespace

TEST(UglyFont, ShippedDefaultTakesShantellAndComesBack) {
  Reader r;
  r.enter(true);
  EXPECT_STREQ(r.name, "ShantellSans");
  r.enter(true);  // a later start changes nothing
  EXPECT_STREQ(r.name, "ShantellSans");
  r.leave();
  EXPECT_STREQ(r.name, "Bokerlam");
  EXPECT_EQ(r.memo, 0);
}

TEST(UglyFont, BuiltinDefaultComesBackAsBuiltin) {
  Reader r;
  r.name[0] = '\0';
  r.enter(true);
  EXPECT_STREQ(r.name, "ShantellSans");
  r.leave();
  EXPECT_STREQ(r.name, "");
}

TEST(UglyFont, UsersOwnFontIsNeverTaken) {
  Reader own;
  std::strcpy(own.name, "Literata");
  own.enter(true);
  own.leave();
  EXPECT_STREQ(own.name, "Literata");
  Reader sans;
  sans.name[0] = '\0';
  sans.builtinDefault = false;  // Noto Sans picked on purpose
  sans.enter(true);
  EXPECT_STREQ(sans.name, "");
}

TEST(UglyFont, AFontPickedInsideTheShellStays) {
  Reader r;
  r.enter(true);
  std::strcpy(r.name, "Literata");
  r.enter(true);  // next start notices the change
  std::strcpy(r.name, "Bokerlam");
  r.enter(true);  // and does not take the font again
  EXPECT_STREQ(r.name, "Bokerlam");
  r.leave();
  EXPECT_STREQ(r.name, "Bokerlam");
}

TEST(UglyFont, AFamilyInstalledLaterTakesOverAtTheNextStart) {
  Reader r;
  r.enter(false);
  EXPECT_STREQ(r.name, "Bokerlam");
  EXPECT_EQ(r.memo, 0);
  r.enter(true);
  EXPECT_STREQ(r.name, "ShantellSans");
}

TEST(UglyFont, DeletingShantellInsideTheShellGivesTheShippedFontBack) {
  Reader r;
  r.enter(true);
  r.name[0] = '\0';  // clearSdFontFamily() once the family is gone
  r.enter(false);
  r.leave();
  EXPECT_STREQ(r.name, "Bokerlam");
  EXPECT_EQ(r.memo, 0);
}

TEST(UglyFont, ShantellPutBackBeforeTheNextStartIsTakenAgain) {
  Reader r;
  r.enter(true);
  r.name[0] = '\0';  // deleted, then installed again in the same session
  r.enter(true);
  EXPECT_STREQ(r.name, "ShantellSans");
  r.leave();
  EXPECT_STREQ(r.name, "Bokerlam");
}

TEST(UglyFont, ABuiltinFontPickedByHandStaysWhateverHappensToShantell) {
  Reader before;
  before.enter(true);
  before.pick("");  // picked while Shantell is on the card
  before.enter(false);  // then Shantell is deleted
  before.leave();
  EXPECT_STREQ(before.name, "");
  Reader after;
  after.enter(true);
  after.name[0] = '\0';  // Shantell deleted first
  after.pick("");  // then a built-in font picked
  after.leave();
  EXPECT_STREQ(after.name, "");
}

TEST(UglyFont, NotoSansPickedInsideTheShellBeforeThisVersionStays) {
  Reader r;  // what v1.0.67 saved after Noto Sans was picked inside the shell
  r.name[0] = '\0';
  r.builtinDefault = false;
  r.memo = 2;
  r.enter(true);
  EXPECT_STREQ(r.name, "");
  EXPECT_EQ(r.memo, 3);
  r.leave();
  EXPECT_STREQ(r.name, "");
}
