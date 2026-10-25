#include "LcpDevice.h"

#include <ArduinoJson.h>
#include <LcpLicense.h>
#include <Logging.h>
#include <ObfuscationUtils.h>
#include <PersistableStore.h>
#include <StreamingJsonParser.h>
#include <Util.h>
#include <WolfsslCrypto.h>
#include <base64.h>

#include <cstring>
#include <memory>
#include <new>

#include "network/HttpDownloader.h"

// Injected from CI secrets into official release builds; self-compiled
// firmware carries the empty default and does everything except LCP.
// Deliberately a plain constant: this is effort asymmetry with per-release
// rotation, not a trust boundary (see /enroll on the service side).
#ifndef FREEINK_LCP_RELEASE_KEY
#define FREEINK_LCP_RELEASE_KEY ""
#endif

namespace lcpdevice {

namespace {

constexpr const char* LCP_ENROLL_URL = "https://lcp.freeink.org/enroll";

// One enrolled identity per device, persisted like the other credential
// stores: the private key obfuscated with the device key, the id plain.
class LcpDeviceStore : public PersistableStore<LcpDeviceStore> {
 public:
  LcpDeviceStore() = default;
  std::string deviceId;
  std::string privHex;  // 64 hex chars

  static const char* getFilePath() { return "/.crosspoint/lcp_device.json"; }
  void toJson(JsonDocument& doc) const {
    doc["device_id"] = deviceId;
    doc["priv_obf"] = obfuscation::obfuscateToBase64(privHex);
  }
  bool fromJson(JsonVariantConst doc) {
    deviceId = doc["device_id"] | "";
    bool ok = false;
    bool tooLong = false;
    privHex = obfuscation::deobfuscateFromBase64(doc["priv_obf"] | "", 64, &ok, &tooLong);
    return true;
  }
  bool valid() const { return !deviceId.empty() && privHex.size() == 64; }
};

// Made and loaded on first use, then kept: only a person borrowing LCP books ever needs it, and the object is
// 64 B of static RAM the X3 would otherwise pay at boot.
std::unique_ptr<LcpDeviceStore> g_store;
LcpDeviceStore* lcpStore() {
  if (!g_store) {
    g_store.reset(new (std::nothrow) LcpDeviceStore());
    if (g_store) g_store->loadFromFile();
  }
  return g_store.get();
}

bool hexToBytes32(const std::string& hex, uint8_t out[32]) {
  if (hex.size() != 64) return false;
  for (int i = 0; i < 32; i++) {
    unsigned v = 0;
    if (sscanf(hex.c_str() + i * 2, "%02x", &v) != 1) return false;
    out[i] = static_cast<uint8_t>(v);
  }
  return true;
}

// {device_id: "..."} from /enroll.
bool parseEnrollResponse(const std::string& response, std::string* deviceId) {
  struct Ctx {
    char pending[16] = {0};
    std::string id;
  } ctx;
  JsonCallbacks callbacks = {};
  callbacks.ctx = &ctx;
  callbacks.onKey = [](void* ud, const char* key, size_t len) {
    auto& c = *static_cast<Ctx*>(ud);
    const size_t n = len < sizeof(c.pending) - 1 ? len : sizeof(c.pending) - 1;
    memcpy(c.pending, key, n);
    c.pending[n] = '\0';
  };
  callbacks.onString = [](void* ud, const char* value, size_t len) {
    auto& c = *static_cast<Ctx*>(ud);
    if (strcmp(c.pending, "device_id") == 0) c.id.assign(value, len < 64 ? len : 64);
  };
  callbacks.onNumber = [](void*, const char*, size_t) {};
  callbacks.onBool = [](void*, bool) {};
  callbacks.onNull = [](void*) {};
  callbacks.onObjectStart = [](void*) {};
  callbacks.onObjectEnd = [](void*) {};
  callbacks.onArrayStart = [](void*) {};
  callbacks.onArrayEnd = [](void*) {};
  StreamingJsonParser parser(callbacks);
  parser.feed(response.data(), response.size());
  if (parser.hasError() || ctx.id.empty()) return false;
  *deviceId = std::move(ctx.id);
  return true;
}

}  // namespace

bool ensureEnrolled(std::string& deviceId, bool& notEnrollable) {
  notEnrollable = false;
  LcpDeviceStore* store = lcpStore();
  if (!store) return false;
  if (store->valid()) {
    deviceId = store->deviceId;
    return true;
  }
  static constexpr char RELEASE_KEY[] = FREEINK_LCP_RELEASE_KEY;
  if (RELEASE_KEY[0] == '\0') {
    notEnrollable = true;
    return false;
  }

  freeink::content::WolfsslCrypto crypto;
  uint8_t priv[32];
  uint8_t pub[32];
  if (!crypto.x25519MakeKey(priv, pub)) {
    LOG_ERR("LCPDEV", "Keygen failed");
    return false;
  }

  std::string request = "{\"release_key\":\"";
  request += RELEASE_KEY;
  request += "\",\"pubkey\":\"";
  request += base64::encode(pub, sizeof(pub)).c_str();
  request += "\"}";

  std::string response;
  int status = 0;
  if (!HttpDownloader::postForm(LCP_ENROLL_URL, request, response, &status) ||
      !parseEnrollResponse(response, &deviceId)) {
    LOG_ERR("LCPDEV", "Enroll failed (status %d)", status);
    return false;
  }

  char privHex[65];
  for (int i = 0; i < 32; i++) snprintf(privHex + i * 2, 3, "%02x", priv[i]);
  store->deviceId = deviceId;
  store->privHex = privHex;
  if (!store->saveToFile()) {
    LOG_ERR("LCPDEV", "Cannot persist device identity");
    return false;
  }
  LOG_INF("LCPDEV", "Enrolled as %s", deviceId.c_str());
  return true;
}

void forget() {
  LcpDeviceStore* store = lcpStore();
  if (!store) return;
  store->deviceId.clear();
  store->privHex.clear();
  store->saveToFile();
}

bool unwrapContentKey(const std::string& epkB64, const std::string& ivB64, const std::string& ctB64,
                      const std::string& tagB64, uint8_t out[32]) {
  using freeink::content::base64Decode;
  uint8_t epk[32];
  uint8_t iv[12];
  uint8_t ct[32];
  uint8_t tag[16];
  if (base64Decode(epkB64.data(), epkB64.size(), epk, sizeof(epk)) != sizeof(epk) ||
      base64Decode(ivB64.data(), ivB64.size(), iv, sizeof(iv)) != sizeof(iv) ||
      base64Decode(ctB64.data(), ctB64.size(), ct, sizeof(ct)) != sizeof(ct) ||
      base64Decode(tagB64.data(), tagB64.size(), tag, sizeof(tag)) != sizeof(tag)) {
    return false;
  }
  LcpDeviceStore* store = lcpStore();
  uint8_t priv[32];
  if (!store || !store->valid() || !hexToBytes32(store->privHex, priv)) return false;

  freeink::content::WolfsslCrypto crypto;
  return freeink::content::lcpUnwrapContentKey(crypto, priv, epk, iv, ct, tag, out);
}

}  // namespace lcpdevice
