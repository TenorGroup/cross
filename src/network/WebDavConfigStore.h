#pragma once

#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <string>
#include <string_view>

struct WebDavConfig {
  std::string url;
  std::string username;
  std::string password;
};

// One WebDAV endpoint is enough for the pull screen. The password stays plain
// only in RAM and is obfuscated in the persisted JSON file.
class WebDavConfigStore : public PersistableStore<WebDavConfigStore> {
 private:
  WebDavConfig config;

  WebDavConfigStore() = default;
  friend class PersistableStore<WebDavConfigStore>;

 public:
  static constexpr size_t MAX_URL_LENGTH = 2048;
  static constexpr size_t MAX_USERNAME_LENGTH = 127;
  static constexpr size_t MAX_PASSWORD_LENGTH = 127;

  static const char* getFilePath() { return "/.crosspoint/webdav.json"; }

  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  static bool isValidUrl(std::string_view url);
  static bool isValid(const WebDavConfig& value);

  bool setConfig(const WebDavConfig& value);
  bool clear();

  const WebDavConfig& getConfig() const { return config; }
  bool isConfigured() const { return !config.url.empty(); }
};

#define WEBDAV_STORE WebDavConfigStore::getInstance()
