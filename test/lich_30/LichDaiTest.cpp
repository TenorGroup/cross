// Bai kiem MAN THONG KE DAI HAN: 3 trang 4 tuan x 7 cot roi trang Theo gio. Trang nao bat dau tu ngay nao, moi
// tuan cong bao nhieu phut va luot lat, truc y dung so dep nao va don vi nao.
#include <gtest/gtest.h>

#include "util/LichDai.h"

namespace {

using readinghistory::Snapshot;

uint32_t thuTu(uint16_t nam, uint8_t thang, uint8_t ngay) { return habits::ordinal(nam, thang, ngay); }

void them(Snapshot& s, uint32_t ma, uint16_t phut, uint16_t lan, uint16_t leMs = 0) {
  solieu::NgayDoc n;
  n.ma = ma;
  n.phut = phut;
  n.trang = 1;
  n.leMs = leMs;
  n.lan = lan;
  s.merge(n);
}

TEST(LichDai, TrangDauLaBonTuanKetThucTuanNay) {
  // 21/09/2026 la thu Hai: trang 1 bat dau thu Hai 31/08, hom nay o hang 4 cot 1.
  const uint32_t homNay = thuTu(2026, 9, 21);
  EXPECT_EQ(habits::dateKey(lichdai::dau(homNay, 0)), 20260831u);
  EXPECT_EQ(habits::dateKey(lichdai::dau(homNay, 1)), 20260803u);
  EXPECT_EQ(habits::dateKey(lichdai::dau(homNay, 2)), 20260706u);
  // Chu nhat 27/09/2026 van o trang co thu Hai 31/08, o cuoi cung.
  EXPECT_EQ(habits::dateKey(lichdai::dau(thuTu(2026, 9, 27), 0)), 20260831u);
  // Thu Hai tiep theo mo tuan moi: trang 1 lui 1 tuan.
  EXPECT_EQ(habits::dateKey(lichdai::dau(thuTu(2026, 9, 28), 0)), 20260907u);
}

TEST(LichDai, TuanCongPhutVaLanDocCuaDung7Ngay) {
  Snapshot s;
  them(s, 20260831, 10, 4, 30000);  // tuan 1, thu Hai
  them(s, 20260906, 20, 6);         // tuan 1, Chu nhat
  them(s, 20260907, 5, 1);          // tuan 2
  them(s, 20260830, 99, 99);        // Chu nhat truoc trang: nam ngoai
  lichdai::Tuan ra[lichdai::SO_TUAN];
  lichdai::tuan(s, thuTu(2026, 8, 31), ra);
  EXPECT_EQ(ra[0].ms, 30u * 60000 + 30000);
  EXPECT_EQ(ra[0].lan, 10u);
  EXPECT_EQ(ra[1].ms, 5u * 60000);
  EXPECT_EQ(ra[1].lan, 1u);
  EXPECT_EQ(ra[2].ms, 0u);
  EXPECT_EQ(ra[3].lan, 0u);
}

TEST(LichDai, PhutTrungBinhMoiLanDocKhongChiaChoKhong) {
  uint32_t phut = 99;
  EXPECT_FALSE(lichdai::trungBinh({}, phut)) << "tuan khong co lan doc: khong co diem";
  EXPECT_FALSE(lichdai::trungBinh({30u * 60000, 0}, phut)) << "co phut ma chua dem lan doc (truoc ban nang cap)";
  ASSERT_TRUE(lichdai::trungBinh({30u * 60000 + 30000, 10}, phut));
  EXPECT_EQ(phut, 3u);  // 30,5 phut / 10 lan
  ASSERT_TRUE(lichdai::trungBinh({100u * 60000, 3}, phut));
  EXPECT_EQ(phut, 33u);  // 33,3 lam tron
  ASSERT_TRUE(lichdai::trungBinh({20000, 1}, phut));
  EXPECT_EQ(phut, 0u);  // duoi nua phut
}

TEST(LichDai, DinhTrucLaSoDepKhongDuoiGiaTriLonNhat) {
  EXPECT_EQ(lichdai::dinh(0), 2u);
  EXPECT_EQ(lichdai::dinh(1), 2u);
  EXPECT_EQ(lichdai::dinh(3), 4u);
  EXPECT_EQ(lichdai::dinh(5), 6u);
  EXPECT_EQ(lichdai::dinh(7), 10u);
  EXPECT_EQ(lichdai::dinh(10), 10u);
  EXPECT_EQ(lichdai::dinh(41), 60u);
  EXPECT_EQ(lichdai::dinh(61), 100u);
  EXPECT_EQ(lichdai::dinh(410), 600u);
  EXPECT_EQ(lichdai::dinh(2001), 4000u);
  for (uint32_t v = 0; v < 5000; ++v) {
    const uint32_t d = lichdai::dinh(v);
    ASSERT_GE(d, v);
    ASSERT_EQ(d % 2, 0u) << "vach giua phai la so nguyen";
  }
}

TEST(LichDai, DonViLaGioTu120PhutTroLen) {
  const auto phut = lichdai::trucThoiGian(119u * 60000);
  EXPECT_FALSE(phut.gio);
  EXPECT_EQ(phut.dinh, 200u);
  const auto gio = lichdai::trucThoiGian(410u * 60000);
  EXPECT_TRUE(gio.gio);
  EXPECT_EQ(gio.dinh, 10u);  // 410 phut = 6,8 gio
  EXPECT_EQ(gio.ms(), 10u * 3600000);
  const auto rong = lichdai::trucThoiGian(0);
  EXPECT_FALSE(rong.gio);
  EXPECT_EQ(rong.dinh, 2u);
}

}  // namespace
