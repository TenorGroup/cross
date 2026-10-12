#include <gtest/gtest.h>

#include <array>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string readSource(const std::string& relativePath) {
  std::ifstream input(std::string(REPO_ROOT_PATH) + "/" + relativePath);
  std::ostringstream contents;
  contents << input.rdbuf();
  return contents.str();
}

size_t countOf(const std::string& source, const std::string& needle) {
  size_t count = 0;
  for (size_t pos = 0; (pos = source.find(needle, pos)) != std::string::npos; pos += needle.size()) ++count;
  return count;
}

std::string normalizeWhitespace(const std::string& source) {
  std::string normalized;
  bool spacing = false;
  for (const unsigned char ch : source) {
    if (std::isspace(ch)) {
      spacing = !normalized.empty();
    } else {
      if (spacing) normalized.push_back(' ');
      normalized.push_back(static_cast<char>(ch));
      spacing = false;
    }
  }
  return normalized;
}

std::string yamlValue(const std::string& language, const std::string& key) {
  std::ifstream input(std::string(REPO_ROOT_PATH) + "/lib/I18n/translations/" + language + ".yaml");
  const std::string prefix = key + ": \"";
  std::string line;
  while (std::getline(input, line)) {
    if (line.starts_with(prefix) && line.ends_with('"')) return line.substr(prefix.size(), line.size() - prefix.size() - 1);
  }
  return {};
}

TEST(ConfirmationProductionContract, ConstructorKeepsLegacyHeadingAndBodyWithOptionalContent) {
  const auto header = normalizeWhitespace(readSource("src/activities/util/ConfirmationActivity.h"));
  const auto source = normalizeWhitespace(readSource("src/activities/util/ConfirmationActivity.cpp"));

  EXPECT_NE(header.find("const std::string& heading"), std::string::npos);
  EXPECT_NE(header.find("const std::string& body, ConfirmationContent content = {}"), std::string::npos);
  EXPECT_NE(source.find("heading(heading), body(body), content(std::move(content))"), std::string::npos);
  EXPECT_NE(source.find("ugly::askBox(renderer, mappedInput, heading.c_str(), body.c_str()"), std::string::npos);
}

TEST(ConfirmationProductionContract, ActivityMapsEveryTerminalDecisionToIsCancelled) {
  const auto source = normalizeWhitespace(readSource("src/activities/util/ConfirmationActivity.cpp"));

  EXPECT_NE(source.find("res.isCancelled = cancelled;"), std::string::npos);
  EXPECT_NE(source.find("const auto decision = confirmationDecision("), std::string::npos);
  EXPECT_NE(source.find("case ConfirmationDecision::Cancel: complete(true);"), std::string::npos);
  EXPECT_NE(source.find("case ConfirmationDecision::Accept: complete(false);"), std::string::npos);
  EXPECT_NE(source.find("case ConfirmationDecision::Move:"), std::string::npos);
  EXPECT_NE(source.find("selectedIndex = 1 - selectedIndex;"), std::string::npos);
}

TEST(ConfirmationProductionContract, ATimedQuestionIsCancelledFromTheLoop) {
  // The cable update asks for 60 s (SdFirmwareUpdateActivity): the loop cancels a question that stood too long,
  // in both shells, before it reads any input.
  const auto source = normalizeWhitespace(readSource("src/activities/util/ConfirmationActivity.cpp"));
  EXPECT_NE(source.find("void ConfirmationActivity::loop() { if (confirmationExpired(content.timeoutMs, shownAt, "
                        "millis())) { complete(true); return; }"),
            std::string::npos);
  EXPECT_NE(source.find("shownAt = millis();"), std::string::npos);
  EXPECT_NE(readSource("src/activities/settings/SdFirmwareUpdateActivity.cpp").find("content.timeoutMs = cableflash::kConsentMs;"),
            std::string::npos);
}

TEST(ConfirmationProductionContract, AllFiveCrossCallersProvideTheirOwnedContent) {
  constexpr std::array<const char*, 4> callers{
      "src/activities/home/FileActions.cpp",  // the file browser and Home's File card ask through it
      "src/activities/home/HomeActivity.cpp",
      "src/activities/settings/FontDownloadActivity.cpp",
      "src/activities/settings/SdFirmwareUpdateActivity.cpp",
  };

  size_t aggregates = 0;
  for (const auto* caller : callers) aggregates += countOf(readSource(caller), "ConfirmationContent{");
  EXPECT_EQ(aggregates, 5U);

  EXPECT_NE(readSource(callers[0]).find("STR_ALERT_FILE_ACTION_LONG"), std::string::npos);
  EXPECT_NE(readSource(callers[1]).find("STR_ALERT_RECENT_TOUCH_MESSAGE"), std::string::npos);
  EXPECT_NE(readSource(callers[2]).find("STR_ALERT_FONT_ACTION_LONG"), std::string::npos);
  EXPECT_NE(readSource(callers[3]).find("STR_ALERT_FIRMWARE_ACTION_LONG"), std::string::npos);
}

struct AlertCopy {
  const char* questionKey;
  const char* messageKey;
  const char* shortKey;
  const char* longKey;
  const char* question;
  const char* message;
  const char* shortAction;
  const char* longAction;
};

TEST(ConfirmationCopy, EnglishAndVietnameseKeepAllSixApprovedVariants) {
  constexpr std::array english{
      AlertCopy{"STR_ALERT_RECENT_QUESTION", "STR_ALERT_RECENT_TOUCH_MESSAGE", "STR_ALERT_RECENT_ACTION_SHORT",
                "STR_ALERT_RECENT_ACTION_LONG", "Remove from Recent?", "The book stays on the SD card.", "Remove",
                "Remove from Recent"},
      AlertCopy{"STR_ALERT_FILE_QUESTION", "STR_ALERT_FILE_MESSAGE_SUFFIX", "STR_ALERT_FILE_ACTION_SHORT",
                "STR_ALERT_FILE_ACTION_LONG", "Delete file?", "will be deleted from the SD card.", "Delete", "Delete file"},
      AlertCopy{"STR_ALERT_FONT_QUESTION", "STR_ALERT_FONT_MESSAGE_SUFFIX", "STR_ALERT_FONT_ACTION_SHORT",
                "STR_ALERT_FONT_ACTION_LONG", "Delete font?", "will be deleted from the SD card.", "Delete", "Delete font"},
      AlertCopy{"STR_STATS_RESET_ALL", "STR_STATS_RESET_ALL_BODY", "STR_ALERT_STATS_ACTION_SHORT", "STR_STATS_RESET_ALL",
                "Clear reading statistics", "Includes per-book statistics and habits.", "Clear", "Clear reading statistics"},
      AlertCopy{"STR_STATS_RESET_HABITS", "STR_STATS_RESET_HABITS_BODY", "STR_ALERT_STATS_HABITS_ACTION_SHORT",
                "STR_STATS_RESET_HABITS", "Restart reading habits", "Keeps reading statistics and positions.", "Restart",
                "Restart reading habits"},
      AlertCopy{"STR_FIRMWARE_UPDATE_PROMPT", nullptr, "STR_ALERT_FIRMWARE_ACTION_SHORT",
                "STR_ALERT_FIRMWARE_ACTION_LONG", "Update firmware?", "firmware.bin", "Update", "Update firmware"},
  };
  constexpr std::array vietnamese{
      AlertCopy{"STR_ALERT_RECENT_QUESTION", "STR_ALERT_RECENT_TOUCH_MESSAGE", "STR_ALERT_RECENT_ACTION_SHORT",
                "STR_ALERT_RECENT_ACTION_LONG", "Bỏ khỏi Gần đây?", "Sách vẫn còn trên thẻ nhớ.", "Bỏ",
                "Bỏ khỏi Gần đây"},
      AlertCopy{"STR_ALERT_FILE_QUESTION", "STR_ALERT_FILE_MESSAGE_SUFFIX", "STR_ALERT_FILE_ACTION_SHORT",
                "STR_ALERT_FILE_ACTION_LONG", "Xóa tệp?", "sẽ bị xóa khỏi thẻ nhớ.", "Xóa", "Xóa tệp"},
      AlertCopy{"STR_ALERT_FONT_QUESTION", "STR_ALERT_FONT_MESSAGE_SUFFIX", "STR_ALERT_FONT_ACTION_SHORT",
                "STR_ALERT_FONT_ACTION_LONG", "Xóa font?", "sẽ bị xóa khỏi thẻ nhớ.", "Xóa", "Xóa font"},
      AlertCopy{"STR_STATS_RESET_ALL", "STR_STATS_RESET_ALL_BODY", "STR_ALERT_STATS_ACTION_SHORT", "STR_STATS_RESET_ALL",
                "Xóa thống kê đọc", "Gồm số liệu từng sách và danh hiệu.", "Xóa", "Xóa thống kê đọc"},
      AlertCopy{"STR_STATS_RESET_HABITS", "STR_STATS_RESET_HABITS_BODY", "STR_ALERT_STATS_HABITS_ACTION_SHORT",
                "STR_STATS_RESET_HABITS", "Tính lại danh hiệu từ đầu", "Giữ số liệu và vị trí đọc.", "Tính lại",
                "Tính lại danh hiệu từ đầu"},
      AlertCopy{"STR_FIRMWARE_UPDATE_PROMPT", nullptr, "STR_ALERT_FIRMWARE_ACTION_SHORT",
                "STR_ALERT_FIRMWARE_ACTION_LONG", "Cập nhật firmware?", "firmware.bin", "Cập nhật",
                "Cập nhật firmware"},
  };

  for (const auto& [language, variants] :
       std::array{std::pair{"english", english}, std::pair{"vietnamese", vietnamese}}) {
    for (const auto& copy : variants) {
      EXPECT_EQ(yamlValue(language, copy.questionKey), copy.question);
      if (copy.messageKey) EXPECT_EQ(yamlValue(language, copy.messageKey), copy.message);
      EXPECT_EQ(yamlValue(language, copy.shortKey), copy.shortAction);
      EXPECT_EQ(yamlValue(language, copy.longKey), copy.longAction);
    }
  }
}

TEST(ConfirmationCopy, VisibleLabelsAreSpecificAndDistinctOnEachDevice) {
  for (const auto* language : {"english", "vietnamese"}) {
    const auto genericConfirm = yamlValue(language, "STR_CONFIRM");
    for (const auto& keys : std::array{
             std::array{"STR_ALERT_RECENT_QUESTION", "STR_ALERT_RECENT_TOUCH_MESSAGE", "STR_ALERT_RECENT_ACTION_SHORT",
                        "STR_ALERT_RECENT_ACTION_LONG"},
             std::array{"STR_ALERT_FILE_QUESTION", "STR_ALERT_FILE_MESSAGE_SUFFIX", "STR_ALERT_FILE_ACTION_SHORT",
                        "STR_ALERT_FILE_ACTION_LONG"},
             std::array{"STR_ALERT_FONT_QUESTION", "STR_ALERT_FONT_MESSAGE_SUFFIX", "STR_ALERT_FONT_ACTION_SHORT",
                        "STR_ALERT_FONT_ACTION_LONG"},
             std::array{"STR_STATS_RESET_ALL", "STR_STATS_RESET_ALL_BODY", "STR_ALERT_STATS_ACTION_SHORT",
                        "STR_STATS_RESET_ALL"},
             std::array{"STR_STATS_RESET_HABITS", "STR_STATS_RESET_HABITS_BODY", "STR_ALERT_STATS_HABITS_ACTION_SHORT",
                        "STR_STATS_RESET_HABITS"},
         }) {
      const auto question = yamlValue(language, keys[0]);
      const auto touchMessage = yamlValue(language, keys[1]);
      const auto shortAction = yamlValue(language, keys[2]);
      const auto longAction = yamlValue(language, keys[3]);
      EXPECT_NE(question, shortAction);
      EXPECT_NE(touchMessage, longAction);
      EXPECT_NE(shortAction, genericConfirm);
      EXPECT_NE(longAction, genericConfirm);
    }
    EXPECT_NE(yamlValue(language, "STR_FIRMWARE_UPDATE_PROMPT"),
              yamlValue(language, "STR_ALERT_FIRMWARE_ACTION_SHORT"));
    EXPECT_NE(yamlValue(language, "STR_ALERT_FIRMWARE_ACTION_SHORT"), genericConfirm);
    EXPECT_NE(yamlValue(language, "STR_ALERT_FIRMWARE_ACTION_LONG"), genericConfirm);
  }
}

TEST(ConfirmationProductionContract, TouchMessageIsWrappedWithoutTruncatingConsequences) {
  const auto source = normalizeWhitespace(readSource("src/activities/util/ConfirmationActivity.cpp"));
  EXPECT_EQ(source.find("renderer.truncatedText(UI_10_FONT_ID, message.c_str()"), std::string::npos);
  EXPECT_NE(source.find("confirmationMessageLines("), std::string::npos);
  EXPECT_NE(source.find("centeredLines(renderer, UI_10_FONT_ID"), std::string::npos);
}

TEST(ConfirmationProductionContract, ButtonDialogPaintsItsOwnFourHintsBeforeRefresh) {
  const auto source = normalizeWhitespace(readSource("src/activities/util/ConfirmationActivity.cpp"));
  EXPECT_NE(source.find("renderer.setRenderMode(GfxRenderer::BW); layoutCross();"), std::string::npos);
  EXPECT_NE(source.find("mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN))"), std::string::npos);
  EXPECT_NE(source.find("GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4)"), std::string::npos);
}

TEST(ConfirmationCopy, StatsQuestionsAndActionsUseMatchingVerbs) {
  EXPECT_EQ(yamlValue("vietnamese", "STR_ALERT_STATS_ALL_QUESTION"), "Xóa thống kê đọc?");
  EXPECT_EQ(yamlValue("vietnamese", "STR_ALERT_STATS_HABITS_QUESTION"), "Tính lại danh hiệu từ đầu?");
  EXPECT_EQ(yamlValue("vietnamese", "STR_ALERT_STATS_ACTION_SHORT"), "Xóa");
  EXPECT_EQ(yamlValue("vietnamese", "STR_ALERT_STATS_HABITS_ACTION_SHORT"), "Tính lại");
  EXPECT_EQ(yamlValue("english", "STR_ALERT_STATS_ALL_QUESTION"), "Clear reading statistics?");
  EXPECT_EQ(yamlValue("english", "STR_ALERT_STATS_HABITS_QUESTION"), "Restart reading habits?");
}

TEST(ConfirmationCopy, ChineseAlertStringsAreTranslated) {
  const auto yaml = readSource("lib/I18n/translations/chinese.yaml");
  std::istringstream input(yaml);
  std::string line;
  size_t count = 0;
  while (std::getline(input, line)) {
    if (!line.starts_with("STR_ALERT_")) continue;
    ++count;
    const auto key = line.substr(0, line.find(':'));
    const auto value = yamlValue("chinese", key);
    EXPECT_NE(value, yamlValue("english", key)) << key;
    EXPECT_TRUE(std::any_of(value.begin(), value.end(), [](unsigned char c) { return c >= 0x80; })) << key;
  }
  EXPECT_GE(count, 16U);
}

}  // namespace
