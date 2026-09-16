#include "SoLieuDoc.h"

#include <algorithm>

namespace solieu {

namespace {
// Cong ma khong cho tran uint16. Mot ngay doc 65.535 phut la 45 ngay lien, tuc khong the
// xay ra; chan o day de neu no xay ra thi so dung im chu khong quay ve 0.
uint16_t congKhongTran(const uint16_t a, const uint16_t b) {
  const uint32_t t = static_cast<uint32_t>(a) + b;
  return t > 0xFFFFu ? 0xFFFFu : static_cast<uint16_t>(t);
}
}  // namespace

void Kho::gopThem(const uint32_t ma, const uint16_t phut, const uint16_t trang) {
  if (phut == 0 && trang == 0) return;  // mot ngay khong doc gi thi khong can mot dong

  if (ma == 0) {
    // Dong ho chua tung dong bo: giu rieng ra. Gan bua vao mot ngay thi lan dong bo dau
    // tien de ra mot chuoi ngay ma, va khong ai go lai duoc.
    phutLac_ += std::min(static_cast<uint32_t>(phut), UINT32_MAX - phutLac_);
    trangLac_ += std::min(static_cast<uint32_t>(trang), UINT32_MAX - trangLac_);
    return;
  }

  if (auto* n = ngayCho(ma)) {
    n->phut = congKhongTran(n->phut, phut);
    n->trang = congKhongTran(n->trang, trang);
  }
}

NgayDoc* Kho::ngayCho(const uint32_t ma) {
  for (auto& n : ngay_)
    if (n.ma == ma) return &n;
  if (ngay_.capacity() < TRAN_NGAY + 1) ngay_.reserve(TRAN_NGAY + 1);
  const auto vt =
      std::lower_bound(ngay_.begin(), ngay_.end(), ma, [](const NgayDoc& a, const uint32_t m) { return a.ma < m; });
  ngay_.insert(vt, NgayDoc{ma, 0, 0, 0});
  if (ngay_.size() > TRAN_NGAY) ngay_.erase(ngay_.begin());
  for (auto& n : ngay_)
    if (n.ma == ma) return &n;
  return nullptr;
}

void Kho::gopMilliseconds(const uint32_t ma, const uint32_t ms, const uint16_t trang) {
  if (ms == 0 && trang == 0) return;
  if (ma == 0) {
    const uint64_t total = std::min<uint64_t>(static_cast<uint64_t>(phutLac_) * 60000 + msLac_ + ms,
                                              static_cast<uint64_t>(UINT32_MAX) * 60000 + 59999);
    phutLac_ = total / 60000;
    msLac_ = total % 60000;
    trangLac_ += std::min<uint32_t>(trang, UINT32_MAX - trangLac_);
  } else if (auto* n = ngayCho(ma)) {
    const uint64_t total = std::min<uint64_t>(static_cast<uint64_t>(n->phut) * 60000 + n->leMs + ms,
                                              static_cast<uint64_t>(UINT16_MAX) * 60000 + 59999);
    n->phut = total / 60000;
    n->leMs = total % 60000;
    n->trang = congKhongTran(n->trang, trang);
  }
}

uint32_t Kho::tongPhut(const int soNgay) const {
  uint32_t t = 0;
  const size_t bo = (soNgay > 0 && static_cast<size_t>(soNgay) < ngay_.size()) ? ngay_.size() - soNgay : 0;
  for (size_t i = bo; i < ngay_.size(); i++) t += ngay_[i].phut;
  return t;
}

uint32_t Kho::tongTrang(const int soNgay) const {
  uint32_t t = 0;
  const size_t bo = (soNgay > 0 && static_cast<size_t>(soNgay) < ngay_.size()) ? ngay_.size() - soNgay : 0;
  for (size_t i = bo; i < ngay_.size(); i++) t += ngay_[i].trang;
  return t;
}

void Kho::xoaHet() {
  ngay_.clear();
  phutLac_ = 0;
  msLac_ = 0;
  trangLac_ = 0;
}

void Kho::datPhanLac(const uint32_t phut, const uint32_t trang) {
  phutLac_ = phut;
  trangLac_ = trang;
}

}  // namespace solieu
