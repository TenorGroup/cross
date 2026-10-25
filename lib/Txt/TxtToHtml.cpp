#include "TxtToHtml.h"

#include <FsHelpers.h>
#include <HeapFloor.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstring>

// v2: bytes that are not UTF-8 are written as U+FFFD, and a CR alone ends a line (v1 kept them and dropped every CR).
// The parser looks for these words in the comment (ChapterHtmlSlimParser::commentHandler).
const char* TxtToHtml::cacheVersionTag(std::string_view filename) {
  return FsHelpers::hasMarkdownExtension(filename) ? "<!-- MD_CACHE_VERSION: 2 -->" : "<!-- TXT_CACHE_VERSION: 2 -->";
}

size_t TxtToHtml::escapedHtmlSize(const std::string_view text) {
  size_t size = 0;
  for (const char c : text) {
    size += c == '&' ? 5 : (c == '<' || c == '>') ? 4 : 1;
  }
  return size;
}

void TxtToHtml::appendEscapedHtml(std::string& out, const std::string_view text) {
  TxtToHtml::streamEscapedHtml([&out](std::string_view fragment) { out.append(fragment); }, text);
}

bool TxtToHtml::stream(std::string_view filename, void* readerCtx, int (*readFn)(void* ctx, uint8_t* buf, size_t size),
                       Print& out) {
  constexpr size_t IN_BUF_SIZE = 8192;
  constexpr size_t OUT_BUF_SIZE = 8192;

  // Two 8 KB blocks, each through the heap-floor door: a refusal ends the conversion cleanly.
  auto inBuf = heapfloor::array<uint8_t[]>(IN_BUF_SIZE, "txt-html-in");
  auto outBuf = heapfloor::array<uint8_t[]>(OUT_BUF_SIZE, "txt-html-out");
  if (!inBuf || !outBuf) {
    LOG_ERR("TXT", "OOM: TXT/MD HTML streaming buffers");
    return false;
  }

  size_t outPos = 0;
  bool outputOk = true;
  auto flushOut = [&]() {
    if (outPos > 0) {
      const size_t written = out.write(outBuf.get(), outPos);
      outputOk = outputOk && (written == outPos);
      outPos = 0;
    }
  };

  auto writeByte = [&](uint8_t b) {
    outBuf[outPos++] = b;
    if (outPos == OUT_BUF_SIZE) flushOut();
  };

  auto writeStr = [&](std::string_view s) {
    for (char c : s) {
      writeByte(static_cast<uint8_t>(c));
    }
  };

  writeStr("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
  writeStr(cacheVersionTag(filename));
  writeByte('\n');
  writeStr("<!DOCTYPE html>\n<html>\n<head><title>");
  std::string title = FsHelpers::getFileNameWithoutExtension(filename);
  TxtToHtml::streamEscapedHtml(writeStr, title);
  writeStr("</title></head>\n<body>\n");

  bool isStart = true;
  bool atLineStart = true;
  size_t pendingSpaces = 0;
  int bytesRead = 0;

  // The parser reads UTF-8 and stops on a byte that is not, so a file in another encoding or with a stray byte
  // would not open at all: every maximal piece that is not UTF-8 is shown as U+FFFD, as browsers do. A character
  // can end in the next read, so the bytes of one wait here until it is whole.
  uint8_t pending[4];
  size_t pendingLen = 0, pendingNeed = 0;
  uint8_t secondLo = 0x80, secondHi = 0xBF;  // the range the byte after a lead byte may take
  bool afterCr = false;                      // the last byte was a CR: a LF right after it is the same line end
  const auto replacement = [&]() { writeStr("\xEF\xBF\xBD"); };
  const auto beginText = [&]() {
    // Spaces held back are written before the first byte of text; a space only before a line end is dropped.
    if (pendingSpaces > 0) {
      for (size_t s = 0; s < pendingSpaces - 1; s++) writeStr("&#160;");
      writeByte(' ');
      pendingSpaces = 0;
    }
    atLineStart = false;
  };
  const auto lineEnd = [&]() {
    pendingSpaces = 0;
    writeStr("<br />");
    atLineStart = true;
  };
  // One byte outside a multi-byte character.
  const auto plain = [&](const uint8_t b) {
    if (b == '\r') {
      lineEnd();
      afterCr = true;
      return;
    }
    const bool skipLf = b == '\n' && afterCr;
    afterCr = false;
    if (skipLf) return;
    if (b == '\n') {
      lineEnd();
      return;
    }
    if (b == ' ') {
      if (atLineStart) {
        writeStr("&#160;");
      } else {
        pendingSpaces++;
      }
      return;
    }
    if (b >= 0x80) {
      beginText();
      if (b >= 0xC2 && b <= 0xF4) {
        pending[0] = b;
        pendingLen = 1;
        pendingNeed = b < 0xE0 ? 1 : b < 0xF0 ? 2 : 3;
        secondLo = b == 0xE0 ? 0xA0 : b == 0xF0 ? 0x90 : 0x80;
        secondHi = b == 0xED ? 0x9F : b == 0xF4 ? 0x8F : 0xBF;
      } else {
        replacement();  // a continuation byte alone, an overlong lead (C0, C1), or past U+10FFFF
      }
      return;
    }
    beginText();
    if (b == '&' || b == '<' || b == '>') {
      const char special = static_cast<char>(b);
      TxtToHtml::streamEscapedHtml(writeStr, std::string_view(&special, 1));
    } else if (b < 0x20 && b != '\t') {
      writeByte(' ');
    } else {
      writeByte(b);
    }
  };

  while ((bytesRead = readFn(readerCtx, inBuf.get(), IN_BUF_SIZE)) > 0) {
    int startIdx = 0;
    if (isStart) {
      isStart = false;
      if (bytesRead >= 3 && inBuf[0] == 0xEF && inBuf[1] == 0xBB && inBuf[2] == 0xBF) {
        startIdx = 3;
      }
    }

    for (int i = startIdx; i < bytesRead; i++) {
      const uint8_t b = inBuf[i];
      if (pendingLen > 0) {
        const bool second = pendingLen == 1;
        if (b >= (second ? secondLo : 0x80) && b <= (second ? secondHi : 0xBF)) {
          pending[pendingLen++] = b;
          if (pendingLen == pendingNeed + 1) {
            // U+FFFE and U+FFFF are no XML characters.
            if (pending[0] == 0xEF && pending[1] == 0xBF && pending[2] >= 0xBE) {
              replacement();
            } else {
              for (size_t k = 0; k < pendingLen; k++) writeByte(pending[k]);
            }
            pendingLen = 0;
          }
          continue;
        }
        // Not the continuation the character needs: what came so far is one invalid piece, and this byte starts afresh.
        replacement();
        pendingLen = 0;
      }
      plain(b);
    }
  }

  if (bytesRead < 0) {
    LOG_ERR("TXT", "Read error while streaming TXT/MD: %.*s", static_cast<int>(filename.size()), filename.data());
    return false;
  }

  if (pendingLen > 0) replacement();  // the file ends inside a character
  writeStr("\n</body>\n</html>\n");
  flushOut();
  if (!outputOk) {
    LOG_ERR("TXT", "Failed to stream complete HTML (write error or disk full)");
    return false;
  }
  return true;
}

bool TxtToHtml::stream(std::string_view filename, std::string_view content, Print& out) {
  struct ViewReader {
    std::string_view s;
    size_t pos = 0;
  } reader{content, 0};

  return stream(
      filename, &reader,
      [](void* ctx, uint8_t* buf, size_t size) -> int {
        auto* r = static_cast<ViewReader*>(ctx);
        if (r->pos >= r->s.size()) return 0;
        const size_t n = std::min(size, r->s.size() - r->pos);
        memcpy(buf, r->s.data() + r->pos, n);
        r->pos += n;
        return static_cast<int>(n);
      },
      out);
}
