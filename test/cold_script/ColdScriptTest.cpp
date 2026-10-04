// The X4 Pro probe's card script: a line taken from the script runs exactly once in order, whatever
// boot dies at whatever point of the commit, and WAIT lines are read as waits.
#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

#include "platform/ColdScript.h"

using cold_script::Take;

namespace {
const std::string kPath = "/x4pro/lenh.txt";
const std::string kScript = "# probe run\r\nCMD:EFUSE\r\n\r\n  CMD:OTA_STATE  \nWAIT 2500\n# the end\nCMD:USB_DRIVE";
const std::vector<std::string> kCommands = {"CMD:EFUSE", "CMD:OTA_STATE", "WAIT 2500", "CMD:USB_DRIVE"};

struct Crash {};

// A card whose every operation can be the one the power cut lands on (counted across boots).
struct FakeCard {
  std::map<std::string, std::string> files;
  int ops = 0;
  int crashAt = 0;
  bool renameFails = false;

  void step() {
    if (++ops == crashAt) throw Crash{};
  }
  bool exists(const char* p) {
    step();
    return files.count(p) != 0;
  }
  bool remove(const char* p) {
    step();
    return files.erase(p) != 0;
  }
  bool rename(const char* from, const char* to) {
    step();
    if (renameFails || !files.count(from) || files.count(to)) return false;
    files[to] = files[from];
    files.erase(from);
    return true;
  }
  bool read(const char* p, std::string& out) {
    step();
    const auto it = files.find(p);
    if (it == files.end()) return false;
    out = it->second;
    return true;
  }
  // Truncate, then fill: a cut in between leaves an empty file.
  bool write(const char* p, const std::string& content) {
    step();
    files[p].clear();
    step();
    files[p] = content;
    return true;
  }
};

// Boots until the script is done. Every command taken is recorded as run; with restartEach the
// command restarts the unit right after it is taken (ROLLBACK_TEST, SD_FLASH, PANIC).
std::vector<std::string> runToEnd(FakeCard& card, const bool restartEach) {
  std::vector<std::string> ran;
  for (int boot = 0; boot < 50; ++boot) {
    try {
      for (;;) {
        std::string line;
        const Take taken = cold_script::take(card, kPath, line);
        if (taken == Take::Done) return ran;
        if (taken == Take::Failed) {
          ran.push_back("FAILED");
          break;
        }
        ran.push_back(line);
        if (restartEach) break;
      }
    } catch (const Crash&) {
    }
  }
  ran.push_back("NEVER_DONE");
  return ran;
}
}  // namespace

TEST(ColdScript, PopLineSkipsCommentsBlanksAndCarriageReturns) {
  std::string line, rest;
  ASSERT_TRUE(cold_script::popLine(kScript, line, rest));
  EXPECT_EQ(line, "CMD:EFUSE");
  ASSERT_TRUE(cold_script::popLine(rest, line, rest));
  EXPECT_EQ(line, "CMD:OTA_STATE");
  EXPECT_EQ(rest, "WAIT 2500\n# the end\nCMD:USB_DRIVE");
  std::string text = rest;
  ASSERT_TRUE(cold_script::popLine(text, line, rest));
  text = rest;
  ASSERT_TRUE(cold_script::popLine(text, line, rest));
  EXPECT_EQ(line, "CMD:USB_DRIVE");
  EXPECT_EQ(rest, "");
  EXPECT_FALSE(cold_script::popLine("# only\n\n  \r\n", line, rest));
  ASSERT_TRUE(cold_script::popLine("\xEF\xBB\xBF" "CMD:BATT\n", line, rest));
  EXPECT_EQ(line, "CMD:BATT");
}

TEST(ColdScript, WaitIsADelayInMilliseconds) {
  uint32_t ms = 0;
  EXPECT_TRUE(cold_script::parseWait("WAIT 2500", ms));
  EXPECT_EQ(ms, 2500u);
  EXPECT_TRUE(cold_script::parseWait("WAIT   0", ms));
  EXPECT_EQ(ms, 0u);
  EXPECT_FALSE(cold_script::parseWait("WAIT", ms));
  EXPECT_FALSE(cold_script::parseWait("WAIT ", ms));
  EXPECT_FALSE(cold_script::parseWait("WAIT 5s", ms));
  EXPECT_FALSE(cold_script::parseWait("WAIT -5", ms));
  EXPECT_FALSE(cold_script::parseWait("WAIT 9999999999", ms));
  EXPECT_FALSE(cold_script::parseWait("CMD:WAIT 5", ms));
}

TEST(ColdScript, WaitEndsAcrossTheMillisWrap) {
  EXPECT_FALSE(cold_script::waitOver(100, 200));
  EXPECT_TRUE(cold_script::waitOver(200, 200));
  EXPECT_TRUE(cold_script::waitOver(0x00000010u, 0xFFFFFFF0u));
  EXPECT_FALSE(cold_script::waitOver(0xFFFFFFF0u, 0x00000010u));
}

TEST(ColdScript, RunsEveryLineOnceAndDeletesTheScript) {
  for (const bool restartEach : {false, true}) {
    FakeCard card;
    card.files[kPath] = kScript;
    EXPECT_EQ(runToEnd(card, restartEach), kCommands);
    EXPECT_TRUE(card.files.empty());
  }
}

TEST(ColdScript, APowerCutAnywhereNeitherRepeatsNorSkipsALine) {
  FakeCard clean;
  clean.files[kPath] = kScript;
  runToEnd(clean, false);
  for (const bool restartEach : {false, true}) {
    for (int at = 1; at <= clean.ops + 2; ++at) {
      FakeCard card;
      card.files[kPath] = kScript;
      card.crashAt = at;
      ASSERT_EQ(runToEnd(card, restartEach), kCommands) << "cut at operation " << at;
      ASSERT_TRUE(card.files.empty()) << "cut at operation " << at;
    }
  }
}

TEST(ColdScript, ALineWhoseCommitFailsDoesNotRun) {
  FakeCard card;
  card.files[kPath] = kScript;
  card.renameFails = true;
  std::string line;
  EXPECT_EQ(cold_script::take(card, kPath, line), Take::Failed);
  EXPECT_EQ(card.files.at(kPath), kScript);
}

TEST(ColdScript, NoScriptIsDone) {
  FakeCard card;
  std::string line;
  EXPECT_EQ(cold_script::take(card, kPath, line), Take::Done);
  card.files[kPath] = "# nothing to run\n";
  EXPECT_EQ(cold_script::take(card, kPath, line), Take::Done);
  EXPECT_TRUE(card.files.empty());
}
