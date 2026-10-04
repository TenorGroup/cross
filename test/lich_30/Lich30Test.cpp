// Bai kiem LUOI 30 NGAY: o nao lay dong nao cua kho, va moi o dam may bac.
//
// Bac theo moc phut co dinh: duoi 1 gio la bac 1, 1 toi duoi 3 gio la bac 2, tu 3 gio la bac 3. Mau mot o
// chi tuy ngay cua no: ngay khac doc nhieu bao nhieu cung khong lam o do nhat di. Hom nay o o cuoi (ra[29]).
#include <gtest/gtest.h>

#include "util/Lich30.h"

namespace {

using solieu::NgayDoc;

constexpr uint32_t PHUT = 60000;

uint32_t thuTu(uint16_t nam, uint8_t thang, uint8_t ngay) { return habits::ordinal(nam, thang, ngay); }
uint32_t maNgay(uint16_t nam, uint8_t thang, uint8_t ngay) { return nam * 10000u + thang * 100u + ngay; }

NgayDoc dong(uint32_t ma, uint16_t phut, uint16_t trang = 1, uint16_t leMs = 0) {
  NgayDoc n;
  n.ma = ma;
  n.phut = phut;
  n.trang = trang;
  n.leMs = leMs;
  return n;
}

TEST(Lich30, KhoTrongThiMoiOBangKhong) {
  uint8_t ra[lich30::SO_O];
  ASSERT_TRUE(lich30::tinh({}, thuTu(2026, 10, 4), ra));
  for (uint8_t o : ra) EXPECT_EQ(o, 0);
}

TEST(Lich30, MayChuaBietNgayThiKhongCoO) {
  uint8_t ra[lich30::SO_O];
  ra[0] = 9;
  EXPECT_FALSE(lich30::tinh({dong(20261004, 30)}, 0, ra));
  for (uint8_t o : ra) EXPECT_EQ(o, 0) << "may chua biet ngay thi khong ve bua";
}

TEST(Lich30, HomNayONguoiCuoiVaNgayThu30ONguoiDau) {
  uint8_t ra[lich30::SO_O];
  lich30::tinh({dong(20260905, 60), dong(20261004, 60)}, thuTu(2026, 10, 4), ra);
  EXPECT_EQ(ra[29], 2);
  EXPECT_EQ(ra[0], 2) << "05/09 la ngay thu 30 tinh ca hom nay";
  for (int i = 1; i < 29; ++i) EXPECT_EQ(ra[i], 0) << i;
}

TEST(Lich30, NgayCuHon30NgayVaNgayTuongLaiKhongVaoLuoi) {
  uint8_t ra[lich30::SO_O];
  // 04/09 la ngay thu 31; 05/10 la ngay mai. Ca hai doc 600 phut.
  lich30::tinh({dong(20260904, 600), dong(20261004, 60), dong(20261005, 600)}, thuTu(2026, 10, 4), ra);
  EXPECT_EQ(ra[29], 2);
  for (int i = 0; i < 29; ++i) EXPECT_EQ(ra[i], 0) << i;
}

TEST(Lich30, BienCuaBaBacTheoMocCoDinh) {
  struct Ca {
    uint16_t phut;
    uint16_t leMs;
    uint8_t muc;
  };
  const Ca ca[] = {{0, 1, 1},      {10, 0, 1},  {59, 59999, 1}, {60, 0, 2},
                   {179, 59999, 2}, {180, 0, 3}, {600, 0, 3}};
  for (const auto& c : ca) {
    uint8_t ra[lich30::SO_O];
    lich30::tinh({dong(20261004, c.phut, 1, c.leMs)}, thuTu(2026, 10, 4), ra);
    EXPECT_EQ(ra[29], c.muc) << c.phut << " phut " << c.leMs << " ms";
  }
}

TEST(Lich30, NgayKhacDocNhieuKhongDoiMauMotO) {
  // 17/09 cua founder: 75 phut. Hom sau doc 280 phut, roi them mot ngay 600 phut: o 17/09 giu nguyen.
  uint8_t ra[lich30::SO_O];
  lich30::tinh({dong(20261001, 75)}, thuTu(2026, 10, 4), ra);
  EXPECT_EQ(ra[26], 2);
  lich30::tinh({dong(20261001, 75), dong(20261002, 280), dong(20261004, 600)}, thuTu(2026, 10, 4), ra);
  EXPECT_EQ(ra[26], 2) << "ngay khac doc nhieu lam o 01/10 nhat di";
  EXPECT_EQ(ra[27], 3);
  // 29 phut va 119 phut khac bac.
  lich30::tinh({dong(20261002, 29), dong(20261003, 119), dong(20261004, 378)}, thuTu(2026, 10, 4), ra);
  EXPECT_EQ(ra[27], 1);
  EXPECT_EQ(ra[28], 2);
}

TEST(Lich30, NgayChiCoLuotLatVanLaNgayDaDoc) {
  uint8_t ra[lich30::SO_O];
  lich30::tinh({dong(20261004, 0, 5)}, thuTu(2026, 10, 4), ra);
  EXPECT_EQ(ra[29], 1);
}

TEST(Lich30, QuaRanhThangVaNamNhuan) {
  uint8_t ra[lich30::SO_O];
  // 02/03/2026: cua so bat dau 01/02 (thang 2 co 28 ngay), 31/01 da ra ngoai.
  lich30::tinh({dong(20260131, 60), dong(20260201, 60), dong(20260228, 60), dong(20260301, 60)},
               thuTu(2026, 3, 2), ra);
  EXPECT_EQ(ra[0], 2);
  EXPECT_EQ(ra[27], 2);
  EXPECT_EQ(ra[28], 2);
  for (int i : {1, 26, 29}) EXPECT_EQ(ra[i], 0) << i;
  // 02/03/2024 la nam nhuan: 29/02 co that, nen 01/03 la o 28 va 29/02 la o 27.
  lich30::tinh({dong(20240229, 60), dong(20240301, 30)}, thuTu(2024, 3, 2), ra);
  EXPECT_EQ(ra[27], 2);
  EXPECT_EQ(ra[28], 1);
  // Qua ranh nam: 05/01/2027 co o dau la 07/12/2026.
  lich30::tinh({dong(maNgay(2026, 12, 7), 60), dong(maNgay(2026, 12, 31), 60)}, thuTu(2027, 1, 5), ra);
  EXPECT_EQ(ra[0], 2);
  EXPECT_EQ(ra[24], 2);
}

TEST(Lich30, ThoiGianLonNhatKhongTran) {
  uint8_t ra[lich30::SO_O];
  lich30::tinh({dong(20261004, 65535, 1, 59999)}, thuTu(2026, 10, 4), ra);
  EXPECT_EQ(ra[29], 3);
}

TEST(Lich30, ThuTuDongTrongKhoKhongAnhHuong) {
  uint8_t xuoi[lich30::SO_O], nguoc[lich30::SO_O];
  const std::vector<NgayDoc> a = {dong(20261002, 30), dong(20261003, 60), dong(20261004, 90)};
  lich30::tinh(a, thuTu(2026, 10, 4), xuoi);
  lich30::tinh({a.rbegin(), a.rend()}, thuTu(2026, 10, 4), nguoc);
  for (int i = 0; i < lich30::SO_O; ++i) EXPECT_EQ(xuoi[i], nguoc[i]) << i;
}

// Bieu do tuan: 4 khung 7 ngay, khung cuoi ket thuc hom nay (04/10: 28/09 toi 04/10). Don vi la phut.
TEST(Lich30Tuan, KhoTrongThiBonKhungBangKhong) {
  uint32_t ra[lich30::SO_TUAN];
  ra[0] = 9;
  ASSERT_TRUE(lich30::tuan({}, thuTu(2026, 10, 4), ra));
  for (uint32_t t : ra) EXPECT_EQ(t, 0u);
}

TEST(Lich30Tuan, MayChuaBietNgayThiKhongCoBieuDo) {
  uint32_t ra[lich30::SO_TUAN];
  ra[0] = 9;
  EXPECT_FALSE(lich30::tuan({dong(20261004, 30)}, 0, ra));
  for (uint32_t t : ra) EXPECT_EQ(t, 0u);
}

TEST(Lich30Tuan, MotNgayDocChiVaoDungMotKhung) {
  uint32_t ra[lich30::SO_TUAN];
  lich30::tuan({dong(20261004, 90)}, thuTu(2026, 10, 4), ra);
  EXPECT_EQ(ra[3], 90u);
  for (int i = 0; i < 3; ++i) EXPECT_EQ(ra[i], 0u) << i;
}

TEST(Lich30Tuan, BienBaKhungVaHaiNgayXaNhatNgoaiBieuDo) {
  uint32_t ra[lich30::SO_TUAN];
  // 28/09 dau khung 3, 27/09 cuoi khung 2, 21/09 dau khung 2, 14/09 dau khung 1, 07/09 dau khung 0,
  // 06/09 va 05/09 (o dau va o thu hai cua luoi) ngoai bieu do.
  lich30::tuan({dong(20260905, 500), dong(20260906, 500), dong(20260907, 1), dong(20260913, 2), dong(20260914, 4),
                dong(20260920, 8), dong(20260921, 16), dong(20260927, 32), dong(20260928, 64), dong(20261004, 128)},
               thuTu(2026, 10, 4), ra);
  EXPECT_EQ(ra[0], 3u);
  EXPECT_EQ(ra[1], 12u);
  EXPECT_EQ(ra[2], 48u);
  EXPECT_EQ(ra[3], 192u);
}

TEST(Lich30Tuan, NgayTuongLaiVaNgayCuHon30NgayKhongVaoKhung) {
  uint32_t ra[lich30::SO_TUAN];
  lich30::tuan({dong(20260904, 600), dong(20261004, 60), dong(20261005, 600)}, thuTu(2026, 10, 4), ra);
  EXPECT_EQ(ra[3], 60u);
  for (int i = 0; i < 3; ++i) EXPECT_EQ(ra[i], 0u) << i;
}

TEST(Lich30Tuan, CongPhutVaMiliGiayLeChuaDuMotPhutBo) {
  uint32_t ra[lich30::SO_TUAN];
  // 59,999 giay cong lai 2 lan = 119,998 giay: tinh theo ms thi ra 1 phut 59 giay, cat xuong 1 phut.
  lich30::tuan({dong(20261003, 0, 1, 59999), dong(20261004, 0, 1, 59999)}, thuTu(2026, 10, 4), ra);
  EXPECT_EQ(ra[3], 1u);
}

TEST(Lich30Tuan, BayNgayToiDaKhongTranUint32) {
  uint32_t ra[lich30::SO_TUAN];
  std::vector<NgayDoc> kho;
  for (int d = 28; d <= 30; ++d) kho.push_back(dong(maNgay(2026, 9, d), 65535, 1, 59999));
  for (int d = 1; d <= 4; ++d) kho.push_back(dong(maNgay(2026, 10, d), 65535, 1, 59999));
  lich30::tuan(kho, thuTu(2026, 10, 4), ra);
  EXPECT_EQ(ra[3], 7u * 65535u + 6u) << "7 ngay x (65535 phut + 59,999 giay)";
}

TEST(Lich30Tuan, ThuTuDongTrongKhoKhongAnhHuong) {
  uint32_t xuoi[lich30::SO_TUAN], nguoc[lich30::SO_TUAN];
  const std::vector<NgayDoc> a = {dong(20260920, 30), dong(20261003, 60), dong(20261004, 90)};
  lich30::tuan(a, thuTu(2026, 10, 4), xuoi);
  lich30::tuan({a.rbegin(), a.rend()}, thuTu(2026, 10, 4), nguoc);
  for (int i = 0; i < lich30::SO_TUAN; ++i) EXPECT_EQ(xuoi[i], nguoc[i]) << i;
}

}  // namespace
