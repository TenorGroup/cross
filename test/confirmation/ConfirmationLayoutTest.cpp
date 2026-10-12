#include <gtest/gtest.h>

#include <type_traits>

#include "components/ConfirmationLayout.h"

namespace {

TEST(ConfirmationLayout, X4ProUsesTheLockedBottomGeometry) {
  constexpr auto layout = confirmationLayout(true, 480, 72, 716, 0, 28);

  static_assert(layout.action.x == 16);
  static_assert(layout.action.y == 532);
  static_assert(layout.action.width == 448);
  static_assert(layout.action.height == 112);
  static_assert(layout.cancel.x == 16);
  static_assert(layout.cancel.y == 656);
  static_assert(layout.cancel.width == 448);
  static_assert(layout.cancel.height == 60);
  static_assert(layout.radius == 20);
  static_assert(layout.messageHeight == 52);
}

TEST(ConfirmationLayout, X3CentersTheWholeDialogInTheContentBody) {
  constexpr auto layout = confirmationLayout(false, 400, 84, 600, 100, 28);

  static_assert(layout.action.x == 17);
  static_assert(layout.action.y == 232);
  static_assert(layout.action.width == 366);
  static_assert(layout.action.height == 220);
  static_assert(layout.cancel.x == 17);
  static_assert(layout.cancel.y == 392);
  static_assert(layout.cancel.width == 366);
  static_assert(layout.cancel.height == 60);
  static_assert(layout.radius == 28);
  static_assert(layout.messageHeight == 100);
}

TEST(ConfirmationLayout, RectangleContainsIncludesTopLeftAndExcludesBottomRight) {
  constexpr ConfirmationBox box{16, 532, 448, 112};

  static_assert(box.contains(16, 532));
  static_assert(box.contains(463, 643));
  static_assert(!box.contains(15, 532));
  static_assert(!box.contains(16, 531));
  static_assert(!box.contains(464, 643));
  static_assert(!box.contains(463, 644));
}

TEST(ConfirmationDecision, X3StartsOnCancelAndOnlyAcceptsTheActionRow) {
  constexpr auto layout = confirmationLayout(false, 400, 84, 600, 100, 28);

  EXPECT_EQ(confirmationDecision(layout, false, 1, false, true, false, false, -1, -1),
            ConfirmationDecision::Cancel);
  EXPECT_EQ(confirmationDecision(layout, false, 0, false, true, false, false, -1, -1),
            ConfirmationDecision::Accept);
  EXPECT_EQ(confirmationDecision(layout, false, 1, false, false, true, false, -1, -1),
            ConfirmationDecision::Move);
  EXPECT_EQ(confirmationDecision(layout, false, 0, true, false, false, false, -1, -1),
            ConfirmationDecision::Cancel);
}

TEST(ConfirmationDecision, X4ProAcceptsOnlyTheActionAreaBelowTheMessage) {
  constexpr auto layout = confirmationLayout(true, 480, 72, 716, 0, 28);

  EXPECT_EQ(confirmationDecision(layout, true, 0, false, false, false, false, -1, -1),
            ConfirmationDecision::Wait);
  EXPECT_EQ(confirmationDecision(layout, true, 0, false, false, false, true, 16, 583),
            ConfirmationDecision::Wait);
  EXPECT_EQ(confirmationDecision(layout, true, 0, false, false, false, true, 16, 584),
            ConfirmationDecision::Accept);
  EXPECT_EQ(confirmationDecision(layout, true, 0, false, false, false, true, 463, 643),
            ConfirmationDecision::Accept);
}

TEST(ConfirmationDecision, X4ProBackAndEveryOutsideTapCancel) {
  constexpr auto layout = confirmationLayout(true, 480, 72, 716, 0, 28);

  EXPECT_EQ(confirmationDecision(layout, true, 0, true, false, false, false, -1, -1),
            ConfirmationDecision::Cancel);
  EXPECT_EQ(confirmationDecision(layout, true, 0, false, false, false, true, 15, 584),
            ConfirmationDecision::Cancel);
  EXPECT_EQ(confirmationDecision(layout, true, 0, false, false, false, true, 16, 644),
            ConfirmationDecision::Cancel);
  EXPECT_EQ(confirmationDecision(layout, true, 0, false, false, false, true, 16, 656),
            ConfirmationDecision::Cancel);
  EXPECT_EQ(confirmationDecision(layout, true, 0, false, false, false, true, 200, 400),
            ConfirmationDecision::Cancel);
}

TEST(ConfirmationLayout, GeometryAndDecisionStateHaveNoDynamicOwnership) {
  static_assert(std::is_trivially_copyable_v<ConfirmationBox>);
  static_assert(std::is_trivially_copyable_v<ConfirmationLayout>);
  static_assert(sizeof(ConfirmationLayout) == 40);
}

TEST(ConfirmationLayout, WrappedTouchMessageGrowsUpwardAndKeepsCancelFixed) {
  for (int lines = 1; lines <= 3; ++lines) {
    const int header = 52 + (lines - 1) * 38;
    const auto layout = confirmationLayout(true, 480, 72, 716, header, 28);
    EXPECT_EQ(layout.messageHeight, header);
    EXPECT_EQ(layout.action.height, header + 60);
    EXPECT_EQ(layout.action.y + layout.action.height + 12, layout.cancel.y);
    EXPECT_EQ(layout.cancel.y + layout.cancel.height, 716);
  }
}

}  // namespace

TEST(ConfirmationLayout, ATimedQuestionIsCancelledOnceItHasStoodItsTime) {
  EXPECT_FALSE(confirmationExpired(0, 0, 4000000000U));  // no limit: it waits
  EXPECT_FALSE(confirmationExpired(60000, 1000, 60999));
  EXPECT_TRUE(confirmationExpired(60000, 1000, 61000));
  EXPECT_FALSE(confirmationExpired(60000, 0xFFFFF000U, 0x00001000U));  // across the millis() wrap
  EXPECT_TRUE(confirmationExpired(60000, 0xFFFFF000U, 0x0000E000U));
}
