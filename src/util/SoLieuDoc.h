#pragma once
#include <cstdint>
#include <vector>

namespace solieu {

struct NgayDoc {
  uint32_t ma = 0;     // ngaygio::maNgay, vi du 20260914
  uint16_t phut = 0;   // so phut doc trong ngay do
  uint16_t trang = 0;  // so trang da lat trong ngay do
  uint16_t leMs = 0;   // milliseconds below one minute, persisted across sessions
};

// So ngay gan nhat giu lai. 30 ngay an 240 byte, du de tra loi "thang nay" ma khong dung
// toi RAM danh cho viec doc sach.
inline constexpr int TRAN_NGAY = 30;

class Kho {
 public:
  // Gop them vao mot ngay. `ma` bang 0 nghia la may CHUA BIET NGAY (dong ho chua tung
  // dong bo), va luc do so lieu vao mot thung rieng chu khong gan bua vao ngay nao: gan
  // bua thi lan dong bo dau tien de ra mot chuoi ngay ma.
  void gopThem(uint32_t ma, uint16_t phut, uint16_t trang);
  void gopMilliseconds(uint32_t ma, uint32_t ms, uint16_t trang = 0);
  uint16_t msChuaBietNgay() const { return msLac_; }
  void datMsChuaBietNgay(uint16_t ms) { msLac_ = ms < 60000 ? ms : 0; }

  // Cac ngay da biet, sap xep theo ngay TANG DAN.
  const std::vector<NgayDoc>& cacNgay() const { return ngay_; }

  uint32_t phutChuaBietNgay() const { return phutLac_; }
  uint32_t trangChuaBietNgay() const { return trangLac_; }

  // Cong don `soNgay` ngay gan nhat trong danh sach. soNgay <= 0 nghia la cong het.
  // KHONG cong phan chua biet ngay vao, vi no khong thuoc ngay nao.
  uint32_t tongPhut(int soNgay = 0) const;
  uint32_t tongTrang(int soNgay = 0) const;

  void xoaHet();

  // Cho phan nap tu the nho dung.
  void datPhanLac(uint32_t phut, uint32_t trang);

 private:
  NgayDoc* ngayCho(uint32_t ma);
  std::vector<NgayDoc> ngay_;
  uint32_t phutLac_ = 0;
  uint32_t trangLac_ = 0;
  uint16_t msLac_ = 0;
};

}  // namespace solieu
