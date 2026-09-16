#pragma once
#include <cstdint>
#include <string_view>

// Phep lich: doi mot moc UTC sang ngay gio DIA PHUONG.
//
// Vi sao no ton tai rieng: HalClock doc duoc ca ngay tu RTC nhung chi dua ra gio va phut,
// va gio do la GIO UTC. Cong mui gio vao thi ngay co the nhay sang hom truoc hoac hom sau,
// va luc do phai dung so ngay cua thang, phai dung nam nhuan. Kho so lieu doc chia theo
// NGAY nen mot loi o day lam lech ca mot ngay so lieu, im lang.
//
// Thuan, khong dung toi phan cung, nen bai kiem chay tren may de ban (test/ngay_gio).
namespace ngaygio {

struct Moc {
  uint16_t nam = 2000;
  uint8_t thang = 1;  // 1-12
  uint8_t ngay = 1;   // 1-31
  uint8_t gio = 0;    // 0-23
  uint8_t phut = 0;

  bool operator==(const Moc& k) const {
    return nam == k.nam && thang == k.thang && ngay == k.ngay && gio == k.gio && phut == k.phut;
  }
};

bool parseUtcOffset(std::string_view text, uint8_t& biasedQuarter);

// So ngay cua mot thang, tinh ca nam nhuan.
uint8_t soNgayTrongThang(uint16_t nam, uint8_t thang);

bool laNamNhuan(uint16_t nam);

// Doi moc UTC sang dia phuong. `phutLech` la so phut lech mui, am hay duong deu duoc.
Moc doiSangDiaPhuong(Moc utc, int phutLech);

// Ma mot ngay, dung lam khoa trong kho so lieu: nam*10000 + thang*100 + ngay.
// Kieu so de so sanh va sap xep duoc ma khong phai dung chuoi.
uint32_t maNgay(const Moc& m);

}  // namespace ngaygio
