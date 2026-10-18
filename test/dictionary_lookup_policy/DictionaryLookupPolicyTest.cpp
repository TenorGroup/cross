#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "util/DictionaryLookupPolicy.h"

namespace {

using DictionaryLookupPolicy::comparatorKey;
using DictionaryLookupPolicy::hasIndexOrderWarning;
using DictionaryLookupPolicy::phraseCandidates;
using DictionaryLookupPolicy::useLinearFallback;

TEST(DictionaryLookupPolicy, VietnameseCaseKeepsDiacritics) {
  EXPECT_EQ(comparatorKey("NGƯỜI ĐƯỜNG"), "người đường");
  EXPECT_NE(comparatorKey("NGƯỜI"), "nguoi");
}

TEST(DictionaryLookupPolicy, NfcAndNfdVietnameseCompareTheSame) {
  EXPECT_EQ(comparatorKey("Đường"), comparatorKey("Đường"));
  EXPECT_EQ(comparatorKey("Đường"), "đường");
}

TEST(DictionaryLookupPolicy, PhraseCandidatesAreThreeTwoOneTokens) {
  EXPECT_EQ(phraseCandidates("  Người đọc sách thêm  "),
            (std::vector<std::string>{"Người đọc sách", "Người đọc", "Người"}));
}

TEST(DictionaryLookupPolicy, PhraseCandidatesHandleUnicodeWhitespace) {
  EXPECT_EQ(phraseCandidates("một\u00A0cuốn\u2003sách"),
            (std::vector<std::string>{"một cuốn sách", "một cuốn", "một"}));
}

TEST(DictionaryLookupPolicy, OrderedIndexDoesNotWarn) {
  EXPECT_FALSE(hasIndexOrderWarning({"aardvark", "người", "Đường"}));
  EXPECT_FALSE(useLinearFallback(false));
}

TEST(DictionaryLookupPolicy, UnsortedIndexWarnsAndUsesLinearFallback) {
  EXPECT_TRUE(hasIndexOrderWarning("Đường", "người"));
  EXPECT_TRUE(hasIndexOrderWarning({"aardvark", "Đường", "người"}));
  EXPECT_TRUE(useLinearFallback(true));
}

}  // namespace
