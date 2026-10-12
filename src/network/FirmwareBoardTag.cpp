#include "FirmwareBoardTag.h"

#include <BoardConfig.h>

#include <cstring>

// The board name derives from the FREEINK_DEVICE_* build flags so every env
// (and any fork built from this source) is tagged automatically. The combined
// X3/X4 ESP32-C3 binary is one compatibility class, tagged "x4".
#if FREEINK_DEVICE_X4PRO
#define CROSSPOINT_BOARD_NAME "x4pro"
#elif FREEINK_DEVICE_X4CLASSIC
#define CROSSPOINT_BOARD_NAME "x4c"
#elif FREEINK_DEVICE_X4 || FREEINK_DEVICE_X3
#define CROSSPOINT_BOARD_NAME "x4"
#elif FREEINK_DEVICE_PAPERMONO
#define CROSSPOINT_BOARD_NAME "papermono"
#elif FREEINK_DEVICE_STICKY
#define CROSSPOINT_BOARD_NAME "sticky"
#elif FREEINK_DEVICE_M5PAPER
#define CROSSPOINT_BOARD_NAME "m5paper"
#elif FREEINK_DEVICE_LILYGO
#define CROSSPOINT_BOARD_NAME "lilygo"
#elif FREEINK_DEVICE_M5
#define CROSSPOINT_BOARD_NAME "m5"
#elif FREEINK_DEVICE_MURPHY
#define CROSSPOINT_BOARD_NAME "murphy"
#elif FREEINK_DEVICE_DELINK
#define CROSSPOINT_BOARD_NAME "delink"
#else
#error "FirmwareBoardTag: no FREEINK_DEVICE_* flag set; cannot derive board name"
#endif

// The X4 Pro cannot be reached in USB download mode: a release without the cable channel would
// leave it with none.
#if FREEINK_DEVICE_X4PRO && !defined(SIMULATOR) && !TENOR_CABLE_FLASH
#error "X4 Pro builds must keep the cable flash channel (-DTENOR_CABLE_FLASH=1)"
#endif
#if TENOR_CABLE_FLASH
#define TENOR_CABLE_FIELD "TENOR-CABLE-V1:1;"
#else
#define TENOR_CABLE_FIELD ""
#endif

namespace board_tag {

namespace {
// The magic's first character must not recur inside it - the scanner restarts
// a failed match with a single-byte lookback, which is only exact then.
constexpr size_t MAGIC_LEN = sizeof("CROSSPOINT-BOARD-V1:") - 1;
}  // namespace

const char TAG[] =
    "CROSSPOINT-BOARD-V1:" CROSSPOINT_BOARD_NAME ";TENOR-CROSS-VERSION-V1:" CROSSPOINT_VERSION ";" TENOR_CABLE_FIELD;

const char* boardName() { return TAG + MAGIC_LEN; }
size_t boardNameLen() { return sizeof(CROSSPOINT_BOARD_NAME) - 1; }

void Scanner::feed(const uint8_t* data, size_t len) {
  if (mismatchFound) return;
  for (size_t i = 0; i < len; i++) {
    const char c = static_cast<char>(data[i]);
#if TENOR_CABLE_FLASH
    if (tailOpen) {
      // The tag is a C string: its fields end at the NUL.
      if (c > 0x20 && c < 0x7F && tailLen < MAX_TAIL) {
        tail[tailLen++] = c;
        continue;
      }
      tailOpen = false;
    }
#endif
    if (capturing) {
      if (c == ';') {
        capturing = false;
        captured[nameLen] = '\0';
        if (nameLen != boardNameLen() || memcmp(captured, boardName(), nameLen) != 0) {
          mismatchFound = true;
          return;
        }
        matchingTagFound = true;
#if TENOR_CABLE_FLASH
        tailOpen = true;
        tailLen = 0;
#endif
      } else if (nameLen < MAX_NAME && c > 0x20 && c < 0x7F) {
        captured[nameLen++] = c;
      } else {
        // Overlong or non-printable: a chance byte-collision, not a real tag.
        capturing = false;
      }
      continue;
    }
    if (c == TAG[magicMatched]) {
      if (++magicMatched == MAGIC_LEN) {
        magicMatched = 0;
        capturing = true;
        nameLen = 0;
      }
    } else {
      magicMatched = (c == TAG[0]) ? 1 : 0;
    }
  }
}

#if TENOR_CABLE_FLASH
bool Scanner::hasField(const char* field, const size_t len) const {
  for (size_t at = 0; at + len <= tailLen; at++) {
    if ((at == 0 || tail[at - 1] == ';') && memcmp(tail + at, field, len) == 0) return true;
  }
  return false;
}

bool Scanner::cable() const { return hasField("TENOR-CABLE-V1:1;", sizeof("TENOR-CABLE-V1:1;") - 1); }

bool Scanner::versionIs(const char* version) const {
  // "TENOR-CROSS-VERSION-V1:" is taken from TAG, so the image holds that key once.
  const char* key = strchr(TAG, ';') + 1;
  const size_t keyLen = strchr(key, ':') - key + 1;
  const size_t versionLen = strlen(version);
  char field[MAX_TAIL + 1];
  if (keyLen + versionLen + 1 > MAX_TAIL) return false;
  memcpy(field, key, keyLen);
  memcpy(field + keyLen, version, versionLen);
  field[keyLen + versionLen] = ';';
  return hasField(field, keyLen + versionLen + 1);
}
#endif

}  // namespace board_tag
