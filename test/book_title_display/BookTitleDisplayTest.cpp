// The display name of a book: the subtitle after ": " goes, the stored title does not change.
#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "src/util/BookTitleDisplay.h"

namespace {

const std::vector<std::pair<std::string, std::string>> kCases = {
    // The subtitle goes: the prefix is 3 letters or more and 5 words or more follow it.
    {"Evergreen Love: Từ Điển Tranh Về Thực Vật", "Evergreen Love"},
    {"Dạ Khúc: Năm Câu Chuyện Về Âm Nhạc Và Đêm Buông", "Dạ Khúc"},
    {"Quái đàm: chuyện yêu quái và dị trùng Nhật Bản", "Quái đàm"},
    {"Án Mạng Trong Phòng Thể Chất: Bí Ẩn Chiếc Ô Đen", "Án Mạng Trong Phòng Thể Chất"},
    {"Turn the Ship Around!: A True Story of Turning Followers into Leaders", "Turn the Ship Around!"},
    {"Accounting Game : Basic Accounting Fresh from the Lemonade Stand", "Accounting Game"},
    {"ABC: One Two Three Four Five", "ABC"},
    {"  ABC  : One   Two Three Four Five", "ABC"},
    // A volume number, a year, a part marker keeps the title whole.
    {"Joker Game 02: Double Joker", "Joker Game 02: Double Joker"},
    {"Joker Game 02: One Two Three Four Five", "Joker Game 02: One Two Three Four Five"},
    {"Nhóc Nicolas: Những Chuyện Chưa Kể 3", "Nhóc Nicolas: Những Chuyện Chưa Kể 3"},
    {"Nhà Trọ Chim Hoàng Yến - Tập 1: Bầy Chim Nơi Khu Vườn Hoang Phế",
     "Nhà Trọ Chim Hoàng Yến - Tập 1: Bầy Chim Nơi Khu Vườn Hoang Phế"},
    {"Series Part IV: One Two Three Four Five", "Series Part IV: One Two Three Four Five"},
    {"Series: Tập IV One Two Three Four Five", "Series: Tập IV One Two Three Four Five"},
    {"2001: One Two Three Four Five", "2001: One Two Three Four Five"},
    // A subtitle shorter than 5 words, a prefix shorter than 3 letters (NFC and NFD), more than one colon.
    {"Samurai Trẻ Tuổi: Ngũ Đại - Hoả", "Samurai Trẻ Tuổi: Ngũ Đại - Hoả"},
    {"Mission: Impossible", "Mission: Impossible"},
    {"ABC: One Two Three Four", "ABC: One Two Three Four"},
    {"ABC: One Two - Four Five", "ABC: One Two - Four Five"},
    {"ÁB: One Two Three Four Five", "ÁB: One Two Three Four Five"},
    {"A\xcc\x81" "B: One Two Three Four Five", "A\xcc\x81" "B: One Two Three Four Five"},
    {"ABC: DEF: One Two Three Four Five", "ABC: DEF: One Two Three Four Five"},
    // No ": " in the title, or nothing in front of it.
    {"Re:Zero", "Re:Zero"},
    {"ABC:One Two Three Four Five", "ABC:One Two Three Four Five"},
    {"ABC - One Two Three Four Five", "ABC - One Two Three Four Five"},
    {"ABC (One Two Three Four Five)", "ABC (One Two Three Four Five)"},
    {"   : One Two Three Four Five", "   : One Two Three Four Five"},
    {"", ""},
};

TEST(BookTitleDisplay, CutsOrKeepsEachTitle) {
  for (const auto& [input, expected] : kCases) EXPECT_EQ(booktitle::display(input), expected) << input;
}

TEST(BookTitleDisplay, TheAnswerIsAViewOfTheTitleSoTheStoredNameIsNotTouched) {
  const std::string title = "Evergreen Love: Từ Điển Tranh Về Thực Vật";
  const std::string copy = title;
  const auto shown = booktitle::display(title);
  EXPECT_EQ(shown, "Evergreen Love");
  EXPECT_EQ(shown.data(), title.data());
  EXPECT_EQ(title, copy);
}

}  // namespace
