#include <gtest/gtest.h>
#include "components/ConfirmationText.h"

namespace {
int width(const char* text) {
  const auto* p = reinterpret_cast<const unsigned char*>(text);
  int count = 0;
  while (utf8NextCodepoint(&p)) ++count;
  return count;
}
std::vector<std::string> wrap(const char* text, int columns, int maxLines) {
  std::vector<std::string> lines;
  const auto* p = reinterpret_cast<const unsigned char*>(text);
  while (*p && static_cast<int>(lines.size()) < maxLines) {
    const auto* begin = p;
    for (int i = 0; i < columns && *p; ++i) utf8NextCodepoint(&p);
    lines.emplace_back(reinterpret_cast<const char*>(begin), p - begin);
  }
  return lines;
}
TEST(ConfirmationText, EightyCharacterFilenameKeepsExtensionAndConsequenceAtEveryTier) {
  const std::string subject = std::string(80, 'a') + ".epub";
  const std::string suffix = " sẽ bị xóa khỏi thẻ nhớ.";
  for (int columns : {28, 23, 19}) {
    const auto lines = confirmationMessageLines(subject + suffix, subject, 3,
        [&](const char* text, int count) { return wrap(text, columns, count); }, width);
    ASSERT_LE(lines.size(), 3U);
    std::string joined;
    for (const auto& line : lines) joined += line;
    EXPECT_TRUE(joined.ends_with(".epub" + suffix)) << joined;
    EXPECT_NE(joined.find("…"), std::string::npos);
  }
}
TEST(ConfirmationText, FixedConsequenceAndShortNamesStayWhole) {
  for (const auto& subject : {std::string{}, std::string{"Be Vietnam Pro"}}) {
    const auto message = subject.empty() ? "Gồm số liệu từng sách và danh hiệu." : subject + " sẽ bị xóa khỏi thẻ nhớ.";
    const auto lines = confirmationMessageLines(message, subject, 3,
        [&](const char* text, int count) { return wrap(text, 19, count); }, width);
    std::string joined;
    for (const auto& line : lines) joined += line;
    EXPECT_EQ(joined, message);
    EXPECT_LE(lines.size(), 3U);
  }
}
}  // namespace
