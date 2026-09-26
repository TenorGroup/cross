#pragma once
#include <cstdint>
#include <functional>
#include <vector>

// JPEGDEC as the page image converter drives it, serving a gray image the test sets
// (hostJpeg) in blocks shaped like the decoder's: MCU rows of `blockRows` source rows,
// cut into pieces `blockCols` wide, left to right, top to bottom.
#define JPEG_SCALE_HALF 2
#define JPEG_SCALE_QUARTER 4
#define JPEG_SCALE_EIGHTH 8
#define JPEG_MODE_PROGRESSIVE 1
#define EIGHT_BIT_GRAYSCALE 5

struct JPEGFILE {
  int32_t iPos;
  int32_t iSize;
  void* fHandle;
};
struct JPEGDRAW {
  int x, y;
  int iWidth, iHeight;
  int iWidthUsed;
  int iBpp;
  uint16_t* pPixels;
  void* pUser;
};
typedef int(JPEG_DRAW_CALLBACK)(JPEGDRAW*);
typedef void*(JPEG_OPEN_CALLBACK)(const char*, int32_t*);
typedef void(JPEG_CLOSE_CALLBACK)(void*);
typedef int32_t(JPEG_READ_CALLBACK)(JPEGFILE*, uint8_t*, int32_t);
typedef int32_t(JPEG_SEEK_CALLBACK)(JPEGFILE*, int32_t);

struct HostJpeg {
  int width = 0, height = 0;
  std::vector<uint8_t> gray;
  int blockRows = 16, blockCols = 128;
};
inline HostJpeg hostJpeg;

class JPEGDEC {
 public:
  int open(const char*, JPEG_OPEN_CALLBACK*, JPEG_CLOSE_CALLBACK*, JPEG_READ_CALLBACK*, JPEG_SEEK_CALLBACK*,
           JPEG_DRAW_CALLBACK* draw) {
    draw_ = draw;
    return 1;
  }
  void close() {}
  int getLastError() const { return 0; }
  int getWidth() const { return hostJpeg.width; }
  int getHeight() const { return hostJpeg.height; }
  int getJPEGType() const { return 0; }
  void setPixelType(int) {}
  void setUserPointer(void* user) { user_ = user; }
  int decode(int, int, int) {
    const auto& img = hostJpeg;
    std::vector<uint8_t> block(static_cast<size_t>(img.blockRows) * img.blockCols);
    for (int y = 0; y < img.height; y += img.blockRows) {
      for (int x = 0; x < img.width; x += img.blockCols) {
        const int used = std::min(img.blockCols, img.width - x);
        for (int r = 0; r < img.blockRows; r++) {
          const int sy = std::min(y + r, img.height - 1);  // rows past the image repeat, as padding
          for (int c = 0; c < img.blockCols; c++) {
            block[r * img.blockCols + c] = img.gray[sy * img.width + std::min(x + c, img.width - 1)];
          }
        }
        JPEGDRAW draw{x, y, img.blockCols, img.blockRows, used, 8, reinterpret_cast<uint16_t*>(block.data()), user_};
        if (!draw_(&draw)) return 0;
      }
    }
    return 1;
  }

 private:
  JPEG_DRAW_CALLBACK* draw_ = nullptr;
  void* user_ = nullptr;
};
