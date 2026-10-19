#include <gtest/gtest.h>

#include "src/shells/UglyFont.h"

namespace {
struct Reader {
  char name[64] = "Bokerlam";
  bool builtinDefault = true;
  uint8_t memo = 0;
  void enter(bool onCard) { uglyfont::take(name, builtinDefault, memo, onCard); }
  void leave() { uglyfont::give(name, memo); }
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
