#include "TimezoneLookup.h"

#include <Logging.h>

#include "CrossPointSettings.h"
#include "HttpDownloader.h"
#include "TimezoneRootCA.h"
#include <NetworkTrust.h>
#include "util/NgayGio.h"

bool timezone_lookup::updateOffset() {
  static uint32_t lastAttemptMs = 0;
  static bool attempted = false;
  static bool previousResult = false;
  static uint8_t previousOffset = 0;
  const uint32_t now = millis();
  if (attempted && now - lastAttemptMs < 15000) {
    if (previousResult) SETTINGS.clockUtcOffsetQ = previousOffset;
    return previousResult;
  }
  attempted = true;
  lastAttemptMs = now;
  previousResult = false;
  if (ESP.getFreeHeap() < HttpDownloader::MIN_TLS_FREE_HEAP ||
      ESP.getMaxAllocHeap() < HttpDownloader::MIN_TLS_MAX_ALLOC) {
    LOG_ERR("TZ", "Insufficient memory for timezone lookup");
    return false;
  }
  // Tenor's own endpoint first (Cloudflare places the caller's IP; the same "+0700" text), ipapi.co after:
  // ipapi.co answers 429 to whole carriers sharing one IP, and a unit that never got an answer shows UTC.
  static constexpr struct {
    const char* url;
    const char* rootCa;
  } SOURCES[] = {{"https://cross.tenor.vn/tz", network_trust::roots},
                 {"https://ipapi.co/utc_offset/", TIMEZONE_ROOT_CA}};
  uint8_t offset = 0;
  bool found = false;
  for (const auto& source : SOURCES) {
    std::string response;
    response.reserve(8);
    const bool ok = HttpDownloader::fetchUrl(
        source.url,
        [&](const uint8_t* bytes, size_t count) {
          if (count > 8 - response.size()) return false;
          response.append(reinterpret_cast<const char*>(bytes), count);
          return true;
        },
        "", "", source.rootCa);
    if (ok && ngaygio::parseUtcOffset(response, offset)) {
      found = true;
      break;
    }
    LOG_ERR("TZ", "Timezone lookup failed at %s", source.url);
  }
  if (!found) {
    LOG_ERR("TZ", "Timezone lookup failed; keeping saved offset");
    return false;
  }
  SETTINGS.clockUtcOffsetQ = offset;
  previousOffset = offset;
  lastAttemptMs = millis();
  LOG_INF("TZ", "UTC offset: %d minutes", (static_cast<int>(offset) - 48) * 15);
  previousResult = true;
  return true;
}
