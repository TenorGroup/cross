#include "NgayGio.h"

namespace ngaygio {

bool parseUtcOffset(std::string_view text, uint8_t& biasedQuarter) {
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.remove_suffix(1);
  if (text.size() != 5 || (text[0] != '+' && text[0] != '-')) return false;
  for (size_t i = 1; i < 5; ++i)
    if (text[i] < '0' || text[i] > '9') return false;
  const int minutes = (text[3] - '0') * 10 + text[4] - '0';
  if (minutes >= 60 || minutes % 15) return false;
  int total = ((text[1] - '0') * 10 + text[2] - '0') * 60 + minutes;
  if (text[0] == '-') total = -total;
  if (total < -720 || total > 840) return false;
  biasedQuarter = static_cast<uint8_t>(total / 15 + 48);
  return true;
}

bool laNamNhuan(const uint16_t nam) { return (nam % 4 == 0 && nam % 100 != 0) || nam % 400 == 0; }

uint8_t soNgayTrongThang(const uint16_t nam, const uint8_t thang) {
  static constexpr uint8_t BANG[13] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (thang < 1 || thang > 12) return 30;
  if (thang == 2 && laNamNhuan(nam)) return 29;
  return BANG[thang];
}

Moc doiSangDiaPhuong(Moc m, const int phutLech) {
  // Gop gio va phut thanh mot so phut trong ngay, cong mui vao, roi dem xem troi may ngay.
  int tong = static_cast<int>(m.gio) * 60 + static_cast<int>(m.phut) + phutLech;
  int ngayTroi = 0;
  while (tong < 0) {
    tong += 1440;
    ngayTroi--;
  }
  while (tong >= 1440) {
    tong -= 1440;
    ngayTroi++;
  }
  m.gio = static_cast<uint8_t>(tong / 60);
  m.phut = static_cast<uint8_t>(tong % 60);

  // Lan lich theo tung ngay. Mui gio that chi lam troi nhieu nhat mot ngay, nhung lan
  // tung ngay thi dung ca khi ai do truyen vao mot so phut lech vo ly.
  while (ngayTroi > 0) {
    if (m.ngay < soNgayTrongThang(m.nam, m.thang)) {
      m.ngay++;
    } else {
      m.ngay = 1;
      if (m.thang == 12) {
        m.thang = 1;
        m.nam++;
      } else {
        m.thang++;
      }
    }
    ngayTroi--;
  }
  while (ngayTroi < 0) {
    if (m.ngay > 1) {
      m.ngay--;
    } else {
      // Lui thang TRUOC roi moi hoi thang do co bao nhieu ngay; hoi truoc la lay nham
      // so ngay cua thang dang dung.
      if (m.thang == 1) {
        m.thang = 12;
        m.nam--;
      } else {
        m.thang--;
      }
      m.ngay = soNgayTrongThang(m.nam, m.thang);
    }
    ngayTroi++;
  }
  return m;
}

uint32_t maNgay(const Moc& m) {
  return static_cast<uint32_t>(m.nam) * 10000u + static_cast<uint32_t>(m.thang) * 100u + m.ngay;
}

}  // namespace ngaygio
