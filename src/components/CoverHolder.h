#pragma once

#include <cstdint>
#include <string>

class Print;

// The picture a Home cover frame shows for a book with no cover. It is made like a real cover:
// a gray master (CoverHolderMaster.h, 8 bit, not dithered) is reduced to the frame's own size and
// dithered by tenor/ink (tenorink::CoverStream, the stream the covers of the X3 and the X4 Pro
// both go through), 4 levels or 1 bit, and cached as ink2_4_WxH.bmp or ink2_1_WxH.bmp. The master
// is inflated one strip at a time into the stream, so no whole gray image is ever held in RAM.
// The cache name is a cover name with a [HEIGHT] slot, so the helper that names a book's thumbnails
// (tenorink::cachePath) names these too. It sits under the firmware's own cache directory: nothing
// is ever written to a book.
namespace coverholder {

// The cache directory, named after the master's crc32 (a new master never meets old files), and
// the cover name inside it, with the [HEIGHT] slot of a book's own thumbnails.
std::string dir();
std::string path();

// Called with each strip of the master in order: `rows` rows of WIDTH bytes starting at row `y`.
using Sink = bool (*)(void* ctx, int y, int rows, const uint8_t* gray);

// Inflates the master one strip at a time (each strip is its own zlib stream, under 4 KB when
// inflated). False when a strip cannot be inflated or the heap floor refuses the decoder's state.
bool decode(Sink sink, void* ctx);

// tenor/ink BMP of the master at w x h (four: 4 levels, else 1 bit). False when the heap floor
// refuses a buffer; the caller then draws a plain frame.
bool writeInk(Print& out, int w, int h, bool four);

}  // namespace coverholder
