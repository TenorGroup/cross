// The decision to restart for a card that stopped answering (src/CardRescue.h).
#include <gtest/gtest.h>

#include <string>

#include "src/CardRescue.h"

namespace {
// A book file whose reads succeed until `deadAfter` reads.
struct FakeBook {
  std::string bytes;
  int deadAfter = 1 << 30;
  size_t pos = 0;
  int read(void* out, size_t n) {
    if (deadAfter-- <= 0) return -1;
    n = std::min(n, bytes.size() - pos);
    bytes.copy(static_cast<char*>(out), n, pos);
    pos += n;
    return static_cast<int>(n);
  }
  bool seekSet(size_t p) {
    if (p > bytes.size()) return false;
    pos = p;
    return true;
  }
};
const std::string kEpub = "PK" + std::string(2000, 'x');

struct ProbeCount {
  int calls = 0;
  bool alive;
  bool operator()() { return ++calls, alive; }
};
}  // namespace

TEST(CardRescueProbe, LiveBookAnswers) {
  FakeBook f{kEpub};
  EXPECT_TRUE(cardrescue::answers(f));
}

TEST(CardRescueProbe, DeadOrWrongFileDoesNot) {
  FakeBook dead{kEpub, 0};
  EXPECT_FALSE(cardrescue::answers(dead));
  FakeBook notZip{"<html>" + std::string(2000, 'x')};
  EXPECT_FALSE(cardrescue::answers(notZip));
  FakeBook tiny{"PK"};
  EXPECT_FALSE(cardrescue::answers(tiny));
  // The card dies after the first sector came back (or it came from SdFat's one-sector cache).
  FakeBook secondSectorLost{kEpub, 1};
  EXPECT_FALSE(cardrescue::answers(secondSectorLost));
}

TEST(CardRescueDecision, RestartsOnlyForADeadCardOnTheX3) {
  cardrescue::rescued = false;
  ProbeCount dead{0, false}, alive{0, true};
  EXPECT_TRUE(cardrescue::shouldRestart(true, dead));
  EXPECT_FALSE(cardrescue::shouldRestart(true, alive));
  EXPECT_EQ(dead.calls + alive.calls, 2);
}

TEST(CardRescueDecision, OtherBoardsNeverProbe) {
  cardrescue::rescued = false;
  ProbeCount dead{0, false};
  EXPECT_FALSE(cardrescue::shouldRestart(false, dead));
  EXPECT_EQ(dead.calls, 0);
}

TEST(CardRescueDecision, OnceAChainHasRestartedNoMoreProbes) {
  cardrescue::rescued = true;
  ProbeCount dead{0, false};
  EXPECT_FALSE(cardrescue::shouldRestart(true, dead));
  EXPECT_EQ(dead.calls, 0);
  cardrescue::rescued = false;
}

TEST(CardRescueChain, SpentRescueSurvivesALaterHeapRestart) {
  cardrescue::onBoot(false, 0);  // cold boot
  ProbeCount dead{0, false};
  ASSERT_TRUE(cardrescue::shouldRestart(true, dead));
  cardrescue::rescued = true;  // silentRestartForCard()
  uint32_t rtc = cardrescue::payload(true);
  cardrescue::onBoot(true, rtc);
  rtc = cardrescue::payload(false);  // the heap restarts the book again, light off
  cardrescue::onBoot(true, rtc);
  EXPECT_EQ(rtc & cardrescue::PAYLOAD_LIGHT_ON, 0u);
  EXPECT_FALSE(cardrescue::shouldRestart(true, dead));
  EXPECT_EQ(dead.calls, 1);
}

TEST(CardRescueChain, ColdBootOrWakeStartsANewChain) {
  cardrescue::rescued = true;
  const uint32_t rtc = cardrescue::payload(true);  // left in RTC memory, read by no silent restart
  cardrescue::onBoot(false, rtc);
  EXPECT_FALSE(cardrescue::rescued);
}

TEST(CardRescueChain, LightBitIsUnchanged) {
  cardrescue::rescued = false;
  EXPECT_EQ(cardrescue::payload(true), cardrescue::PAYLOAD_LIGHT_ON);
  EXPECT_EQ(cardrescue::payload(false), 0u);
  cardrescue::onBoot(true, cardrescue::PAYLOAD_LIGHT_ON);
  EXPECT_FALSE(cardrescue::rescued);
}
