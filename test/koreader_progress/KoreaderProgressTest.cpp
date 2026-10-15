// The reply of a sync server, read by the production KOReaderSyncClient::getProgress. A device the server
// answers with null (or leaves out) is no device: the result screen shows a "Device" line only for a name.
#include <HalMemory.h>
#include <HeapFloor.h>
#include <KOReaderCredentialStore.h>
#include <KOReaderSyncClient.h>
#include <SecureHttpClient.h>
#include <gtest/gtest.h>

EspClass ESP;
unsigned long millis() { return freeink::SecureHttpClient::nowMs; }
void delay(unsigned long ms) { freeink::SecureHttpClient::nowMs += static_cast<uint32_t>(ms); }
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
  freeink::SecureHttpClient::replyChunkBytes = 0;
  freeink::SecureHttpClient::advanceMsPerChunk = 0;
  freeink::SecureHttpClient::usedStreamingGet = false;
  KOReaderSyncClient::requestDeadlineMs = 0;
  heapfloor::allow = true;
  heapfloor::calls = 0;
  heapfloor::lastBytes = 0;
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

TEST(KoreaderProgress, AnAbsoluteDeadlineUsesTheCancellableStreamingPathAndKeepsTheSameReply) {
  freeink::SecureHttpClient::replyCode = 200;
  freeink::SecureHttpClient::replyBody = R"({"progress":"x","percentage":0.25,"timestamp":7})";
  freeink::SecureHttpClient::nowMs = 100;
  freeink::SecureHttpClient::replyChunkBytes = 7;
  freeink::SecureHttpClient::advanceMsPerChunk = 1;
  freeink::SecureHttpClient::usedStreamingGet = false;
  KOReaderSyncClient::requestDeadlineMs = 1000;
  heapfloor::allow = true;
  heapfloor::calls = 0;

  KOReaderProgress progress;
  EXPECT_EQ(KOReaderSyncClient::getProgress("hash", progress), KOReaderSyncClient::OK);
  EXPECT_TRUE(freeink::SecureHttpClient::usedStreamingGet);
  EXPECT_EQ(progress.progress, "x");
  EXPECT_FLOAT_EQ(progress.percentage, 0.25f);
}

TEST(KoreaderProgress, TheAbsoluteDeadlineStopsAReplyAcrossMillisWrap) {
  freeink::SecureHttpClient::replyCode = 200;
  freeink::SecureHttpClient::replyBody = R"({"progress":"x","percentage":0.25,"timestamp":7})";
  freeink::SecureHttpClient::nowMs = 0xFFFFFFF0u;
  freeink::SecureHttpClient::replyChunkBytes = 8;
  freeink::SecureHttpClient::advanceMsPerChunk = 8;
  KOReaderSyncClient::requestDeadlineMs = 0x00000008u;
  heapfloor::allow = true;

  KOReaderProgress progress;
  EXPECT_EQ(KOReaderSyncClient::getProgress("hash", progress), KOReaderSyncClient::NETWORK_ERROR);
}

TEST(KoreaderProgress, ABodyGrowthAtFourKilobytesUsesTheHeapFloorAndFailsCleanly) {
  freeink::SecureHttpClient::replyCode = 200;
  freeink::SecureHttpClient::replyBody.assign(5000, 'x');
  freeink::SecureHttpClient::nowMs = 10;
  freeink::SecureHttpClient::replyChunkBytes = 5000;
  KOReaderSyncClient::requestDeadlineMs = 1000;
  heapfloor::allow = false;
  heapfloor::calls = 0;
  heapfloor::lastBytes = 0;

  KOReaderProgress progress;
  EXPECT_EQ(KOReaderSyncClient::getProgress("hash", progress), KOReaderSyncClient::LOW_MEMORY);
  EXPECT_EQ(heapfloor::calls, 1u);
  EXPECT_GE(heapfloor::lastBytes, heapfloor::kBigBytes);
}

}  // namespace
