#include <cstring>

#include "BlePageTurnerJson.h"

namespace bleturner {
namespace {

void copyField(char* dest, const char* src, const size_t maxLen) {
  strncpy(dest, src, maxLen - 1);
  dest[maxLen - 1] = '\0';
}

}  // namespace

void writeJson(const Config& config, JsonDocument& doc) {
  doc["blePageTurnerEnabled"] = config.enabled;
  doc["blePeerAddr"] = config.peerAddr;
  doc["blePeerName"] = config.peerName;
  doc["blePrevKeyUsage"] = config.prevKeyUsage;
  doc["bleNextKeyUsage"] = config.nextKeyUsage;
  // One entry per remote: its address and its 32-bit slots. An empty table is written too:
  // the user cleared it, and the built-in default must not come back.
  if (config.remoteCount > 0) {
    JsonArray remotes = doc["bleRemotes"].to<JsonArray>();
    for (uint8_t i = 0; i < config.remoteCount && i < kMaxRemotes; i++) {
      JsonObject r = remotes.add<JsonObject>();
      r["addr"] = config.remotes[i].addr;
      JsonArray binds = r["binds"].to<JsonArray>();
      for (uint8_t j = 0; j < config.remotes[i].count && j < kMaxBindings; j++) binds.add(config.remotes[i].bindings[j]);
    }
  }
}

bool readJson(Config& config, JsonVariantConst doc) {
  // A file without the keys means nobody used the page turner: the defaults stay (off, no
  // remote, nothing learned).
  config.enabled = (doc["blePageTurnerEnabled"] | uint8_t{0}) ? 1 : 0;
  copyField(config.peerAddr, doc["blePeerAddr"] | "", sizeof(config.peerAddr));
  copyField(config.peerName, doc["blePeerName"] | "", sizeof(config.peerName));
  config.prevKeyUsage = doc["blePrevKeyUsage"] | uint8_t{0};
  config.nextKeyUsage = doc["bleNextKeyUsage"] | uint8_t{0};
  config.remoteCount = 0;
  for (const JsonObjectConst r : doc["bleRemotes"].as<JsonArrayConst>()) {
    if (config.remoteCount >= kMaxRemotes) break;
    const char* addr = r["addr"] | "";
    if (addr[0] == '\0') continue;
    // Build the entry aside first: the document's string may point into this very slot.
    RemoteTable t{};
    copyField(t.addr, addr, sizeof(t.addr));
    for (const JsonVariantConst b : r["binds"].as<JsonArrayConst>()) {
      if (t.count >= kMaxBindings) break;
      // A slot is kept only when every bit means something (BleKeyBinding.h valid()).
      if (b.is<uint32_t>() && valid(b.as<uint32_t>())) t.bindings[t.count++] = b.as<uint32_t>();
    }
    config.remotes[config.remoteCount++] = t;
  }
  return !doc["blePageTurnerEnabled"].isNull();
}

}  // namespace bleturner
