#include "TimezoneLookup.h"

#include <Logging.h>

#include "CrossPointSettings.h"
#include "HttpDownloader.h"
#include "TimezoneRootCA.h"
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
  std::string response;
  response.reserve(8);
  const bool ok = HttpDownloader::fetchUrl(
      "https://ipapi.co/utc_offset/",
      [&](const uint8_t* bytes, size_t count) {
        if (count > 8 - response.size()) return false;
        response.append(reinterpret_cast<const char*>(bytes), count);
        return true;
      },
      "", "", TIMEZONE_ROOT_CA);
  uint8_t offset = 0;
  if (!ok || !ngaygio::parseUtcOffset(response, offset)) {
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
