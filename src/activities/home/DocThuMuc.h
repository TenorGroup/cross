#pragma once
#include <algorithm>
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
// `tran` > 0 la so muc toi da: vuot thi dung doc, `ra` rong, *quaTran = true (ham van tra ve true). Man
// nao danh sach lon theo so tep tren the thi dat tran; 0 la khong gioi han, nhu moi noi goi cu.
bool doc(const char* duongDan, bool hienFileAn, Loc loc, char* dem, size_t demCo, std::vector<std::string>& ra,
         size_t tran = 0, bool* quaTran = nullptr);

// How many names a folder page may hold, from the heap left: the one decision for every screen that lists a
// card folder (tenor/cross Home and File, and the tenor/ugly Folder page). Each row costs a string slot (the
// vector doubles, so twice that must fit in one block) and its name on the heap, and some heap stays for the
// frame and the card library. 0 means refuse the folder rather than run out. Pass the result as `tran`.
inline constexpr size_t FOLDER_MAX_ROWS = 2000, FOLDER_HEAP_KEEP = 16 * 1024, FOLDER_BYTES_PER_ROW = 80;
inline size_t tran(const size_t freeHeap, const size_t largestBlock) {
  const size_t byFree = freeHeap > FOLDER_HEAP_KEEP ? (freeHeap - FOLDER_HEAP_KEEP) / FOLDER_BYTES_PER_ROW : 0;
  const size_t byBlock = largestBlock / (2 * sizeof(std::string));
  return std::min({FOLDER_MAX_ROWS, byFree, byBlock});
}

// Adjacent visible sibling folder in natural order, wrapping at either end.
// Holds only candidate names, never the whole parent directory in RAM.
std::string neighbor(const std::string& path, bool showHidden, int direction, char* buffer, size_t capacity);

}  // namespace docthumuc
