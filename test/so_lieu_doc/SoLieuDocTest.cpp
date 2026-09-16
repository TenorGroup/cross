// Bai kiem LOI KHO SO LIEU DOC.
//
// No ton tai vi truoc 14/09/2026 may ghi DUNG BANG KHONG so lieu doc theo thoi gian, nen
// man Thong ke chi bay duoc mot dong dem so sach. Kho nay la thu de ra nhung con so that.
//
// Hai cho de vo am tham va bai kiem nay giu ca hai: gop nham mot ngay thanh hai dong, va
// gan bua so lieu vao mot ngay khi dong ho chua tung dong bo.
#include <gtest/gtest.h>

#include "util/SoLieuDoc.h"

namespace {

TEST(KhoSoLieu, NgayMoiThiThemMotDong) {
  solieu::Kho k;
  k.gopThem(20260914, 12, 30);
  ASSERT_EQ(k.cacNgay().size(), 1u);
  EXPECT_EQ(k.cacNgay()[0].ma, 20260914u);
  EXPECT_EQ(k.cacNgay()[0].phut, 12);
  EXPECT_EQ(k.cacNgay()[0].trang, 30);
}

TEST(KhoSoLieu, CungNgayThiCongDonChuKhongThemDong) {
  solieu::Kho k;
  k.gopThem(20260914, 12, 30);
  k.gopThem(20260914, 8, 20);
  ASSERT_EQ(k.cacNgay().size(), 1u) << "mot ngay ra hai dong la so lieu bi chia doi";
  EXPECT_EQ(k.cacNgay()[0].phut, 20);
  EXPECT_EQ(k.cacNgay()[0].trang, 50);
}

TEST(KhoSoLieu, DanhSachLuonSapTheoNgayTangDan) {
  solieu::Kho k;
  k.gopThem(20260915, 1, 1);
  k.gopThem(20260913, 1, 1);
  k.gopThem(20260914, 1, 1);
  ASSERT_EQ(k.cacNgay().size(), 3u);
  EXPECT_EQ(k.cacNgay()[0].ma, 20260913u);
  EXPECT_EQ(k.cacNgay()[1].ma, 20260914u);
  EXPECT_EQ(k.cacNgay()[2].ma, 20260915u);
}

// Qua tran thi bo ngay CU NHAT, khong phai bo ngay vua ghi.
TEST(KhoSoLieu, QuaTranThiBoNgayCuNhat) {
  solieu::Kho k;
  for (int i = 0; i < solieu::TRAN_NGAY; i++) k.gopThem(20260101u + i, 1, 1);
  ASSERT_EQ(static_cast<int>(k.cacNgay().size()), solieu::TRAN_NGAY);
  const uint32_t cuNhat = k.cacNgay().front().ma;

  k.gopThem(20260201, 5, 5);
  EXPECT_EQ(static_cast<int>(k.cacNgay().size()), solieu::TRAN_NGAY) << "khong duoc phinh qua tran";
  EXPECT_NE(k.cacNgay().front().ma, cuNhat) << "ngay cu nhat phai bi bo";
  EXPECT_EQ(k.cacNgay().back().ma, 20260201u) << "ngay vua ghi phai con";
}

// Dong ho chua tung dong bo thi may KHONG biet hom nay la ngay may. Gan bua vao mot ngay
// la lan dong bo dau tien de ra mot chuoi ngay ma.
TEST(KhoSoLieu, ChuaBietNgayThiVaoThungRieng) {
  solieu::Kho k;
  k.gopThem(0, 15, 40);
  EXPECT_TRUE(k.cacNgay().empty()) << "khong duoc de ra mot dong ngay gia";
  EXPECT_EQ(k.phutChuaBietNgay(), 15u);
  EXPECT_EQ(k.trangChuaBietNgay(), 40u);
}

TEST(KhoSoLieu, ThungChuaBietNgayCungCongDon) {
  solieu::Kho k;
  k.gopThem(0, 15, 40);
  k.gopThem(0, 5, 10);
  EXPECT_EQ(k.phutChuaBietNgay(), 20u);
  EXPECT_EQ(k.trangChuaBietNgay(), 50u);
}

TEST(KhoSoLieu, ThungChuaBietNgayKhongQuayVeKhongKhiTran) {
  solieu::Kho k;
  k.datPhanLac(UINT32_MAX - 1, UINT32_MAX - 2);
  k.gopThem(0, 2, 3);
  EXPECT_EQ(k.phutChuaBietNgay(), UINT32_MAX);
  EXPECT_EQ(k.trangChuaBietNgay(), UINT32_MAX);
}

TEST(KhoSoLieu, CongDonMayNgayGanNhat) {
  solieu::Kho k;
  k.gopThem(20260911, 10, 1);
  k.gopThem(20260912, 20, 2);
  k.gopThem(20260913, 30, 3);
  EXPECT_EQ(k.tongPhut(1), 30u);
  EXPECT_EQ(k.tongPhut(2), 50u);
  EXPECT_EQ(k.tongPhut(0), 60u) << "khong truyen so ngay thi cong het";
  EXPECT_EQ(k.tongTrang(2), 5u);
}

// Phan chua biet ngay KHONG duoc cong vao tong theo ngay, vi no khong thuoc ngay nao.
TEST(KhoSoLieu, TongTheoNgayKhongOmPhanChuaBietNgay) {
  solieu::Kho k;
  k.gopThem(20260913, 30, 3);
  k.gopThem(0, 100, 100);
  EXPECT_EQ(k.tongPhut(0), 30u);
  EXPECT_EQ(k.tongTrang(0), 3u);
}

TEST(KhoSoLieu, GopKhongThiKhongDeRaDongNao) {
  solieu::Kho k;
  k.gopThem(20260914, 0, 0);
  EXPECT_TRUE(k.cacNgay().empty()) << "mot ngay khong doc gi thi khong can mot dong";
}

}  // namespace

TEST(KhoSoLieu, ShortSessionsKeepTheirFractionAcrossDatesAndUnknownTime) {
  solieu::Kho k;
  k.gopMilliseconds(20260914, 25000);
  k.gopMilliseconds(20260915, 11000);
  k.gopMilliseconds(20260914, 40000, 1);
  ASSERT_EQ(k.cacNgay().size(), 2u);
  EXPECT_EQ(k.cacNgay()[0].phut, 1);
  EXPECT_EQ(k.cacNgay()[0].leMs, 5000);
  EXPECT_EQ(k.cacNgay()[1].leMs, 11000);
  k.gopMilliseconds(0, 59000);
  k.gopMilliseconds(0, 2000);
  EXPECT_EQ(k.phutChuaBietNgay(), 1u);
  EXPECT_EQ(k.msChuaBietNgay(), 1000);
}
