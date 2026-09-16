// Bai kiem PHEP LICH doi UTC sang gio dia phuong.
//
// No ton tai vi kho so lieu doc chia theo NGAY, va RTC cua may tra GIO UTC. Cong mui gio
// vao thi ngay nhay sang hom truoc hoac hom sau, va luc do phai dung so ngay cua thang,
// phai dung nam nhuan. Sai o day thi ca mot ngay so lieu roi nham o, im lang, va khong
// bao gio co ai bao.
//
// Mui gio Viet Nam la UTC+7, tuc 420 phut, nen mo ca canh nua dem la chuyen thuong ngay.
#include <gtest/gtest.h>

#include "util/NgayGio.h"

namespace {

using ngaygio::Moc;

TEST(NamNhuan, BonTramVaMotTram) {
  EXPECT_TRUE(ngaygio::laNamNhuan(2028));
  EXPECT_FALSE(ngaygio::laNamNhuan(2026));
  EXPECT_FALSE(ngaygio::laNamNhuan(2100)) << "chia het 100 ma khong chia het 400 thi khong nhuan";
  EXPECT_TRUE(ngaygio::laNamNhuan(2000)) << "chia het 400 thi nhuan";
}

TEST(SoNgayTrongThang, ThangHaiTheoNamNhuan) {
  EXPECT_EQ(ngaygio::soNgayTrongThang(2026, 2), 28);
  EXPECT_EQ(ngaygio::soNgayTrongThang(2028, 2), 29);
  EXPECT_EQ(ngaygio::soNgayTrongThang(2026, 1), 31);
  EXPECT_EQ(ngaygio::soNgayTrongThang(2026, 4), 30);
}

TEST(DoiMui, KhongLechThiGiuNguyen) {
  const Moc m{2026, 9, 14, 10, 30};
  EXPECT_EQ(ngaygio::doiSangDiaPhuong(m, 0), m);
}

TEST(DoiMui, CongMuiTrongCungNgay) {
  const Moc ra = ngaygio::doiSangDiaPhuong(Moc{2026, 9, 14, 3, 30}, 420);
  EXPECT_EQ(ra, (Moc{2026, 9, 14, 10, 30}));
}

// Day la ca hay sai nhat: 23 gio UTC cong 7 tieng la SANG NGAY HOM SAU.
TEST(DoiMui, CongMuiVuotNuaDemThiSangNgayHomSau) {
  const Moc ra = ngaygio::doiSangDiaPhuong(Moc{2026, 9, 14, 23, 0}, 420);
  EXPECT_EQ(ra, (Moc{2026, 9, 15, 6, 0}));
}

TEST(DoiMui, TruMuiVuotNuaDemThiVeNgayHomTruoc) {
  const Moc ra = ngaygio::doiSangDiaPhuong(Moc{2026, 9, 14, 3, 0}, -300);
  EXPECT_EQ(ra, (Moc{2026, 9, 13, 22, 0}));
}

TEST(DoiMui, VuotSangThangSau) {
  const Moc ra = ngaygio::doiSangDiaPhuong(Moc{2026, 9, 30, 23, 0}, 420);
  EXPECT_EQ(ra, (Moc{2026, 10, 1, 6, 0}));
}

TEST(DoiMui, LuiVeThangTruocThiLayDungSoNgayCuaThangDo) {
  const Moc ra = ngaygio::doiSangDiaPhuong(Moc{2026, 10, 1, 3, 0}, -300);
  EXPECT_EQ(ra, (Moc{2026, 9, 30, 22, 0})) << "thang 9 co 30 ngay";
}

TEST(DoiMui, VuotSangNamSau) {
  const Moc ra = ngaygio::doiSangDiaPhuong(Moc{2026, 12, 31, 23, 0}, 420);
  EXPECT_EQ(ra, (Moc{2027, 1, 1, 6, 0}));
}

TEST(DoiMui, LuiVeNamTruoc) {
  const Moc ra = ngaygio::doiSangDiaPhuong(Moc{2027, 1, 1, 3, 0}, -300);
  EXPECT_EQ(ra, (Moc{2026, 12, 31, 22, 0}));
}

// Nam nhuan: 28/02/2028 luc 23 gio cong 7 tieng phai ra 29/02, khong duoc nhay sang 01/03.
TEST(DoiMui, NamNhuanCoNgayHaiChinThangHai) {
  const Moc ra = ngaygio::doiSangDiaPhuong(Moc{2028, 2, 28, 23, 0}, 420);
  EXPECT_EQ(ra, (Moc{2028, 2, 29, 6, 0}));
}

TEST(DoiMui, NamKhongNhuanThiThangHaiChiCoHaiTam) {
  const Moc ra = ngaygio::doiSangDiaPhuong(Moc{2026, 2, 28, 23, 0}, 420);
  EXPECT_EQ(ra, (Moc{2026, 3, 1, 6, 0}));
}

// Mui gio le 45 phut van co that tren doi, vi du Nepal UTC+5:45.
TEST(DoiMui, MuiLePhutVanDung) {
  const Moc ra = ngaygio::doiSangDiaPhuong(Moc{2026, 9, 14, 18, 30}, 345);
  EXPECT_EQ(ra, (Moc{2026, 9, 15, 0, 15}));
}

TEST(MaNgay, SapXepDuocTheoThoiGian) {
  EXPECT_EQ(ngaygio::maNgay(Moc{2026, 9, 14, 0, 0}), 20260914u);
  EXPECT_LT(ngaygio::maNgay(Moc{2026, 9, 30, 0, 0}), ngaygio::maNgay(Moc{2026, 10, 1, 0, 0}));
  EXPECT_LT(ngaygio::maNgay(Moc{2026, 12, 31, 0, 0}), ngaygio::maNgay(Moc{2027, 1, 1, 0, 0}));
}

}  // namespace

TEST(UtcOffset, StrictResponseAndQuarterHourBoundaries) {
  uint8_t result = 9;
  for (const auto& [text, expected] :
       {std::pair{"+0700", 76}, {"+0545", 71}, {"-0330", 34}, {"+1400", 104}, {"-1200", 0}, {"+0000\n", 48}}) {
    ASSERT_TRUE(ngaygio::parseUtcOffset(text, result));
    EXPECT_EQ(result, expected);
  }
  for (const auto text : {"", "{}", "+1460", "+1401", "-1215", "+0559", "+05:45", "0700", "+0700oops"}) {
    result = 76;
    EXPECT_FALSE(ngaygio::parseUtcOffset(text, result));
    EXPECT_EQ(result, 76);
  }
}
