// The cable channel rule (board_tag::dropsCable) on the X4 Classic, built as its release is from v1.1.0: the
// X4 Pro's TCF1 channel, so an image without it (v1.0.76 and older) is refused by the cable session and the OTA.
#include <gtest/gtest.h>

#include <string>

#include "network/FirmwareBoardTag.h"

namespace {
board_tag::Scanner scan(const std::string& tag) {
  const std::string bytes = std::string("\x01\x02", 2) + tag + std::string(1, '\0') + "xyz";
  board_tag::Scanner s;
  s.feed(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
  return s;
}
}  // namespace

TEST(X4ClassicCablePolicy, ThisBuildCarriesTheChannel) {
  EXPECT_EQ(std::string(board_tag::TAG),
            "CROSSPOINT-BOARD-V1:x4c;TENOR-CROSS-VERSION-V1:" CROSSPOINT_VERSION ";TENOR-CABLE-V1:1;");
  EXPECT_FALSE(board_tag::dropsCable(scan(board_tag::TAG)));
}

TEST(X4ClassicCablePolicy, OlderX4ClassicAndOtherBoardsAreDropped) {
  const auto old = scan("CROSSPOINT-BOARD-V1:x4c;TENOR-CROSS-VERSION-V1:v1.0.76;");
  EXPECT_TRUE(old.matched());
  EXPECT_TRUE(board_tag::dropsCable(old));
  const auto pro = scan("CROSSPOINT-BOARD-V1:x4pro;TENOR-CROSS-VERSION-V1:v1.1.0;TENOR-CABLE-V1:1;");
  EXPECT_TRUE(pro.mismatch());
  EXPECT_TRUE(board_tag::dropsCable(pro));
}
