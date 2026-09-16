#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Doc mot thu muc tren the nho thanh danh sach ten doc duoc.
//
// Vi sao tach ra: truoc 14/09/2026 phep nay chi song trong FileBrowserActivity, va khi
// man chinh can bay thang noi dung the nho thi cach re nhat la CHEP no sang. Chep thi
// hai ban troi xa nhau: mot ban hoc them duoi file moi, ban kia khong, va nguoi dung
// thay hai danh sach khac nhau cho cung mot thu muc.
//
// Cai gi la "muc doc duoc", cai gi bi giau, va thu tu sap xep: tat ca chot o day, mot cho.
namespace docthumuc {

// Loai muc can lay.
enum class Loc : uint8_t {
  Sach,      // sach va anh: epub, xtc, txt, md, bmp, png
  Firmware,  // chi file .bin, cho man nap firmware tu the nho
};

// Ghi ten cac muc vao `ra`, thu muc mang dau '/' o cuoi, da sap xep theo FsHelpers.
// `dem` la vung nho muon de hung ten file; ham nay KHONG tu cap phat.
// Tra ve false khi khong mo duoc thu muc, va luc do `ra` rong.
bool doc(const char* duongDan, bool hienFileAn, Loc loc, char* dem, size_t demCo, std::vector<std::string>& ra);

// Adjacent visible sibling folder in natural order, wrapping at either end.
// Holds only candidate names, never the whole parent directory in RAM.
std::string neighbor(const std::string& path, bool showHidden, int direction, char* buffer, size_t capacity);

}  // namespace docthumuc
