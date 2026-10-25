#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <string>
#include <vector>

/**
 * Per-provider LCP passphrase hashes (hex sha256(passphrase)), keyed by the
 * license's `provider` URI, so borrowing another book from the same library
 * skips the passphrase prompt: the saved hash is tried against /unlock first
 * and only a 403 re-prompts. Stored XOR-obfuscated with the device key, the
 * same protection as OPDS bearer tokens — the hash is the basic-profile user
 * key, so it is never written in the clear.
 */
class LcpPassphraseStore : public PersistableStore<LcpPassphraseStore> {
 private:
  struct Entry {
    std::string provider;
    std::string userKeyHex;
  };
  std::vector<Entry> entries;
  static constexpr size_t MAX_PROVIDERS = 8;

  LcpPassphraseStore() = default;
  friend class PersistableStore<LcpPassphraseStore>;
  static LcpPassphraseStore* instance();
  std::string find(const std::string& provider) const;
  bool upsert(const std::string& provider, const std::string& userKeyHex);

 public:
  static const char* getFilePath() { return "/.crosspoint/lcp_passphrases.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  // The stored hash for a provider (empty when none).
  // Both made and loaded on the first call (a person borrowing LCP books), then kept: the base class's
  // getInstance() would hold the object in static RAM from boot. An empty/false answer when the heap has no room.
  static std::string get(const std::string& provider);
  // Upserts the hash for a provider; empty removes the entry. No-op (no SD
  // write) when unchanged. Returns true on success.
  static bool put(const std::string& provider, const std::string& userKeyHex);
};

