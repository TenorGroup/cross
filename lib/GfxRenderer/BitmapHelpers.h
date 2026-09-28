#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

#include "../Memory/Memory.h"

struct BmpHeader;

// Helper functions
uint8_t quantize(int gray, int x, int y);
uint8_t quantizeSimple(int gray);
int adjustPixel(int gray);

struct GrayPlanePixel {
  bool write;
  bool black;
};

// level: 0=black, 1=dark, 2=light, 3=white. drawPixel(true) clears a bit.
constexpr GrayPlanePixel grayPlanePixel(uint8_t level, bool msb, bool absolute) {
  if (absolute) return {true, !(level == 3 || level == (msb ? 2 : 1))};
  return {msb ? (level == 1 || level == 2) : level == 1, false};
}

enum class BmpRowOrder { BottomUp, TopDown };

// Every cover thumbnail has the Recent card's cover shape (HOME_CARD_COVER_W x HOME_CARD_COVER_H in
// HomeExcerptStyle.h), so the card draws it 1:1. The earlier 0.6 x height came out narrower than
// the card for most covers, and the card stretched it by repeating columns and rows.
constexpr int THUMB_SHAPE_W = 236;
constexpr int THUMB_SHAPE_H = 356;
constexpr int thumbWidthFor(const int height) { return height * THUMB_SHAPE_W / THUMB_SHAPE_H; }

// A source scaled to fill (`crop`) or to fit inside a target, in whole pixels. The side that
// decides the scale comes out at exactly the target: a float scale lost a pixel there for many
// source sizes, and a thumbnail one column short of the card is stretched again.
void coverScaleSize(int srcW, int srcH, int targetW, int targetH, bool crop, int* outW, int* outH);

// Populates a 1-bit BMP header in the provided memory.
void createBmpHeader(BmpHeader* bmpHeader, int width, int height, BmpRowOrder rowOrder);

// 1-bit Floyd-Steinberg on serpentine rows, for every 1-bit picture (cover thumbnails, the X3
// sleep cover). The Atkinson ditherer it replaces spread only 6/8 of each error: flat tones drifted
// (32 came out black, 224 white) and the thumbnail seen at reading distance was five times further
// from its cover. One row of carried error, not two: each slot of the next row is written once this
// row has read it, and the one slot still unread goes in `pending`. Callers visit x in at(i) order:
// odd rows run right to left, which breaks up the diagonal worms of a one-way pass.
class FloydSteinberg1BitDitherer {
 public:
  explicit FloydSteinberg1BitDitherer(int width) : width(width), err(new (std::nothrow) int16_t[width + 2]()) {}
  ~FloydSteinberg1BitDitherer() { delete[] err; }
  FloydSteinberg1BitDitherer(const FloydSteinberg1BitDitherer&) = delete;
  FloydSteinberg1BitDitherer& operator=(const FloydSteinberg1BitDitherer&) = delete;

  // Callers must check the row allocation before processing pixels.
  bool isValid() const { return err != nullptr; }
  // The i-th pixel of this row to visit.
  int at(const int i) const { return (row & 1) ? width - 1 - i : i; }

  // 1 = white.
  uint8_t processPixel(const int gray, const int x) {
    int v = gray + err[x + 1] + right;
    v = v < 0 ? 0 : v > 255 ? 255 : v;
    const uint8_t white = v >= 128;
    const int e = v - (white ? 255 : 0);
    right = (e * 7) >> 4;
    err[(row & 1) ? x + 2 : x] += (e * 3) >> 4;  // below and behind: already read
    err[x + 1] = ((e * 5) >> 4) + pending;       // below: read just now
    pending = e >> 4;                            // below and ahead: not read yet
    return white;
  }

  void nextRow() {
    row++;
    right = pending = 0;
    err[0] = err[width + 1] = 0;  // the slots beside the row
  }

 private:
  const int width;
  int16_t* err;
  int row = 0, right = 0, pending = 0;
};

// Atkinson dithering - distributes only 6/8 (75%) of error for cleaner results
// Error distribution pattern:
//     X  1/8 1/8
// 1/8 1/8 1/8
//     1/8
// Less error buildup = fewer artifacts than Floyd-Steinberg
class AtkinsonDitherer {
 public:
  explicit AtkinsonDitherer(int width, bool originalThresholds = false) : originalThresholds(originalThresholds) {
    if (width <= 0) return;
    const size_t candidateRowSize = static_cast<size_t>(width) + 4;
    if (candidateRowSize > SIZE_MAX / (3 * sizeof(int16_t))) return;
    rowSize = candidateRowSize;
    errorRows = makeUniqueNoThrow<int16_t[]>(rowSize * 3);
    if (!errorRows) return;
    errorRow0 = errorRows.get();
    errorRow1 = errorRow0 + rowSize;
    errorRow2 = errorRow1 + rowSize;
  }

  // Callers must check row allocation before processing pixels.
  bool isValid() const { return errorRows != nullptr; }

  // **1. EXPLICITLY DELETE THE COPY CONSTRUCTOR**
  AtkinsonDitherer(const AtkinsonDitherer& other) = delete;

  // **2. EXPLICITLY DELETE THE COPY ASSIGNMENT OPERATOR**
  AtkinsonDitherer& operator=(const AtkinsonDitherer& other) = delete;

  uint8_t processPixel(int gray, int x) {
    // Add accumulated error
    int adjusted = gray + (isValid() ? errorRow0[x + 2] : 0);
    if (adjusted < 0) adjusted = 0;
    if (adjusted > 255) adjusted = 255;

    // Quantize to 4 levels
    uint8_t quantized;
    int quantizedValue;
    if (originalThresholds) {
      if (adjusted < 43) {
        quantized = 0;
        quantizedValue = 0;
      } else if (adjusted < 128) {
        quantized = 1;
        quantizedValue = 85;
      } else if (adjusted < 213) {
        quantized = 2;
        quantizedValue = 170;
      } else {
        quantized = 3;
        quantizedValue = 255;
      }
    } else {  // Legacy panel tuning; slightly darker midtones.
      if (adjusted < 30) {
        quantized = 0;
        quantizedValue = 15;
      } else if (adjusted < 55) {
        quantized = 1;
        quantizedValue = 35;
      } else if (adjusted < 150) {
        quantized = 2;
        quantizedValue = 90;
      } else {
        quantized = 3;
        quantizedValue = 210;
      }
    }

    if (!isValid()) return quantized;

    // Calculate error (only distribute 6/8 = 75%)
    int error = (adjusted - quantizedValue) >> 3;  // error/8

    // Distribute 1/8 to each of 6 neighbors
    errorRow0[x + 3] += error;  // Right
    errorRow0[x + 4] += error;  // Right+1
    errorRow1[x + 1] += error;  // Bottom-left
    errorRow1[x + 2] += error;  // Bottom
    errorRow1[x + 3] += error;  // Bottom-right
    errorRow2[x + 2] += error;  // Two rows down

    return quantized;
  }

  void nextRow() {
    if (!isValid()) return;
    int16_t* temp = errorRow0;
    errorRow0 = errorRow1;
    errorRow1 = errorRow2;
    errorRow2 = temp;
    memset(errorRow2, 0, rowSize * sizeof(int16_t));
  }

  void reset() {
    if (!isValid()) return;
    memset(errorRow0, 0, rowSize * sizeof(int16_t));
    memset(errorRow1, 0, rowSize * sizeof(int16_t));
    memset(errorRow2, 0, rowSize * sizeof(int16_t));
  }

 private:
  size_t rowSize{0};
  std::unique_ptr<int16_t[]> errorRows;
  int16_t* errorRow0 = nullptr;
  int16_t* errorRow1 = nullptr;
  int16_t* errorRow2 = nullptr;
  const bool originalThresholds;
};

// Floyd-Steinberg error diffusion dithering with serpentine scanning
// Serpentine scanning alternates direction each row to reduce "worm" artifacts
// Error distribution pattern (left-to-right):
//       X   7/16
// 3/16 5/16 1/16
// Error distribution pattern (right-to-left, mirrored):
// 1/16 5/16 3/16
//      7/16  X
class FloydSteinbergDitherer {
 public:
  explicit FloydSteinbergDitherer(int width, bool originalThresholds = false)
      : rowCount(0), originalThresholds(originalThresholds) {
    if (width <= 0) return;
    const size_t candidateRowSize = static_cast<size_t>(width) + 2;
    if (candidateRowSize > SIZE_MAX / (2 * sizeof(int16_t))) return;
    rowSize = candidateRowSize;
    errorRows = makeUniqueNoThrow<int16_t[]>(rowSize * 2);
    if (!errorRows) return;
    errorCurRow = errorRows.get();
    errorNextRow = errorCurRow + rowSize;
  }

  // Callers must check row allocation before processing pixels.
  bool isValid() const { return errorRows != nullptr; }

  // **1. EXPLICITLY DELETE THE COPY CONSTRUCTOR**
  FloydSteinbergDitherer(const FloydSteinbergDitherer& other) = delete;

  // **2. EXPLICITLY DELETE THE COPY ASSIGNMENT OPERATOR**
  FloydSteinbergDitherer& operator=(const FloydSteinbergDitherer& other) = delete;

  // Process a single pixel and return quantized 2-bit value
  // x is the logical x position (0 to width-1), direction handled internally
  uint8_t processPixel(int gray, int x) {
    // Add accumulated error to this pixel
    int adjusted = gray + (isValid() ? errorCurRow[x + 1] : 0);

    // Clamp to valid range
    if (adjusted < 0) adjusted = 0;
    if (adjusted > 255) adjusted = 255;

    // Quantize to 4 levels (0, 85, 170, 255)
    uint8_t quantized;
    int quantizedValue;
    if (originalThresholds) {
      if (adjusted < 43) {
        quantized = 0;
        quantizedValue = 0;
      } else if (adjusted < 128) {
        quantized = 1;
        quantizedValue = 85;
      } else if (adjusted < 213) {
        quantized = 2;
        quantizedValue = 170;
      } else {
        quantized = 3;
        quantizedValue = 255;
      }
    } else {  // Legacy panel tuning; slightly darker midtones.
      if (adjusted < 30) {
        quantized = 0;
        quantizedValue = 15;
      } else if (adjusted < 55) {
        quantized = 1;
        quantizedValue = 35;
      } else if (adjusted < 150) {
        quantized = 2;
        quantizedValue = 90;
      } else {
        quantized = 3;
        quantizedValue = 210;
      }
    }

    if (!isValid()) return quantized;

    // Calculate error
    int error = adjusted - quantizedValue;

    // Distribute error to neighbors (serpentine: direction-aware)
    if (!isReverseRow()) {
      // Left to right: standard distribution
      // Right: 7/16
      errorCurRow[x + 2] += (error * 7) >> 4;
      // Bottom-left: 3/16
      errorNextRow[x] += (error * 3) >> 4;
      // Bottom: 5/16
      errorNextRow[x + 1] += (error * 5) >> 4;
      // Bottom-right: 1/16
      errorNextRow[x + 2] += (error) >> 4;
    } else {
      // Right to left: mirrored distribution
      // Left: 7/16
      errorCurRow[x] += (error * 7) >> 4;
      // Bottom-right: 3/16
      errorNextRow[x + 2] += (error * 3) >> 4;
      // Bottom: 5/16
      errorNextRow[x + 1] += (error * 5) >> 4;
      // Bottom-left: 1/16
      errorNextRow[x] += (error) >> 4;
    }

    return quantized;
  }

  // Call at the end of each row to swap buffers
  void nextRow() {
    if (!isValid()) return;
    // Swap buffers
    int16_t* temp = errorCurRow;
    errorCurRow = errorNextRow;
    errorNextRow = temp;
    // Clear the next row buffer
    memset(errorNextRow, 0, rowSize * sizeof(int16_t));
    rowCount++;
  }

  // Check if current row should be processed in reverse
  bool isReverseRow() const { return (rowCount & 1) != 0; }

  // Reset for a new image or MCU block
  void reset() {
    if (!isValid()) return;
    memset(errorCurRow, 0, rowSize * sizeof(int16_t));
    memset(errorNextRow, 0, rowSize * sizeof(int16_t));
    rowCount = 0;
  }

 private:
  int rowCount;
  size_t rowSize{0};
  std::unique_ptr<int16_t[]> errorRows;
  int16_t* errorCurRow = nullptr;
  int16_t* errorNextRow = nullptr;
  const bool originalThresholds;
};
// Packed levels are 0=black through 3=white. Destination indexes are in bits.
inline void writeAbsoluteGrayRow(const uint8_t* levels, int width, int firstBit, int stepBits, uint8_t* lsb,
                                 uint8_t* msb) {
  for (int x = 0, bit = firstBit; x < width; ++x, bit += stepBits) {
    const uint8_t level = (levels[x / 4] >> (6 - (x % 4) * 2)) & 3;
    const uint8_t mask = 0x80 >> (bit % 8);
    if (!(level & 1)) lsb[bit / 8] &= ~mask;
    if (!(level & 2)) msb[bit / 8] &= ~mask;
  }
}
