// The reply of a sync server, read by the production KOReaderSyncClient::getProgress. A device the server
// answers with null (or leaves out) is no device: the result screen shows a "Device" line only for a name.
#include <HalMemory.h>
#include <KOReaderCredentialStore.h>
#include <KOReaderSyncClient.h>
#include <SecureHttpClient.h>
#include <gtest/gtest.h>

EspClass ESP;
HalMemory::HeapStats HalMemory::getDefaultHeap() { return {200000, 300000, 150000, 100000}; }

// The settings the client reads: credentials set, a plain server, no protocol extension.
bool KOReaderCredentialStore::hasCredentials() const { return true; }
std::string KOReaderCredentialStore::getMd5Password() const { return "md5"; }
std::string KOReaderCredentialStore::getBaseUrl() const { return "http://sync.test"; }
bool KOReaderCredentialStore::usesCrossPointSyncServer() const { return false; }

namespace {

KOReaderProgress fetch(const char* body) {
  freeink::SecureHttpClient::replyCode = 200;
  freeink::SecureHttpClient::replyBody = body;
  KOReaderProgress progress;
  EXPECT_EQ(KOReaderSyncClient::getProgress("hash", progress), KOReaderSyncClient::OK);
  return progress;
}

TEST(KoreaderProgress, ANamedDeviceIsKept) {
  const auto p = fetch(R"({"progress":"/body/p[3]","percentage":0.5,"device":"Kindle","device_id":"k1","timestamp":9})");
  EXPECT_EQ(p.device, "Kindle");
  EXPECT_EQ(p.deviceId, "k1");
  EXPECT_EQ(p.timestamp, 9);
}

TEST(KoreaderProgress, ANullDeviceIsEmptyNotTheWordNull) {
  const auto p = fetch(R"({"progress":"/body/p[3]","percentage":0.5,"device":null,"device_id":null,"timestamp":9})");
  EXPECT_TRUE(p.device.empty()) << p.device;
}

TEST(KoreaderProgress, AMissingOrEmptyDeviceIsEmpty) {
  EXPECT_TRUE(fetch(R"({"progress":"x","percentage":0.1,"timestamp":1})").device.empty());
  EXPECT_TRUE(fetch(R"({"progress":"x","percentage":0.1,"device":"","timestamp":1})").device.empty());
}

}  // namespace
