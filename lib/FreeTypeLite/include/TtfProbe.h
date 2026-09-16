#pragma once
#include <cstdint>

namespace ttfprobe {

// 229 diem ma: 146 chu Viet cong 83 ky tu ASCII in duoc. Bang nam o flash.
extern const uint16_t BO_CHU[];
extern const uint16_t BO_CHU_DAI;

struct KetQua {
  bool moDuoc = false;
  const char* loi = nullptr;

  uint16_t soChuToDuoc = 0;
  uint16_t soChuThieu = 0;   // font khong co chu do
  uint32_t tongDiemAnh = 0;  // tong so diem anh da to, de biet no lam that

  uint32_t msMoFont = 0;  // mo tep, doc bang, dung mat font
  uint32_t msDatCo = 0;
  uint32_t msToChu = 0;  // phan nang: to het 229 chu
  uint32_t msTong = 0;

  uint32_t dinhBoNhoFt = 0;  // dinh so byte FreeType tu cap phat
  uint32_t heapTruoc = 0;
  uint32_t heapThapNhat = 0;
  uint32_t heapSau = 0;  // phai tro ve gan bang heapTruoc, khong thi co ro ri

  uint32_t soLanDocThe = 0;  // moi lan la mot luot qua khoa cua HalStorage
  uint32_t soByteDocThe = 0;

  // So lan nhuong cho luong ranh chay. Moi lan ton mot nhip he thong, nen thoi gian to
  // chu o tren DA GOM phan nay; tru ra neu muon con so thuan tinh toan.
  uint32_t soLanNhuong = 0;

  // Ngan xep: cap bao nhieu, va luc cang nhat con du bao nhieu. Con du gan 0 nghia la
  // sat mep, va sat mep tren chip nay la mot lan "Stack protection fault".
  uint32_t nganXepCap = 0;
  uint32_t nganXepConDu = 0;
};

// demRong: so byte cua bo dem doc the. 0 nghia la TAT bo dem, moi lan FreeType doi du
// lieu la mot luot xuong the. Chay ca hai de thay bo dem dang mua duoc bao nhieu.
// CANH BAO, do duoc bang mau ngay 14/09/2026: ham nay KHONG duoc goi tren luong chinh.
// Bo thong dich TrueType va bo to duong cong an ngan xep sau hon ngan xep cua luong do,
// va may dung ngay bang "Stack protection fault" trong task loopTask. Phai chay no trong
// mot task rieng co ngan xep rong. Xem cach goi o src/main.cpp, lenh CMD:TTF_PROBE.
KetQua chay(const char* duongDanFont, uint8_t pt, uint16_t demRong);

// Cach goi DUNG: dung mot task rieng co ngan xep rong, roi doi no xong. Luong goi bi chan
// trong luc doi nhung van vo watchdog deu, nen may khong bi da.
KetQua chayTrenTaskRieng(const char* duongDanFont, uint8_t pt, uint16_t demRong, uint32_t nganXepByte = 32768);

}  // namespace ttfprobe
