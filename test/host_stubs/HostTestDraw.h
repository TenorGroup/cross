#pragma once

#include <string>
#include <vector>

// Moi lan tang ve duoc yeu cau ve chu, no ghi mot dong vao day. Bai kiem nho do
// doc duoc NHAN THUC SU hien ra, thay vi chi doan tu ma.
struct DrawnText {
  std::string text;
  int x = 0;
  int y = 0;
  bool rotated = false;  // nhan nut canh duoc ve xoay, ky tu truyen vao khac ky tu nhin thay
};

namespace hosttest {
std::vector<DrawnText>& drawn();
void clearDrawn();
bool wasDrawn(const std::string& text);
}  // namespace hosttest
