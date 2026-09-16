#include <gtest/gtest.h>
#include <wolfssl/ssl.h>

#include "../../freeink-sdk/libs/network/SecureNet/include/SecureClient.h"
TEST(SecureClientLifecycle, MissingCaFailsBeforeTransportOrAllocation) {
  wire::reset();
  const int before = tls_fixture::outstandingMethods;
  {
    freeink::SecureClient client;
    EXPECT_EQ(client.connect("example.test", 443), 0);
  }
  EXPECT_TRUE(wire::requests.empty());
  EXPECT_EQ(tls_fixture::outstandingMethods, before);
}
TEST(SecureClientLifecycle, TcpFailureDoesNotLeakMethod) {
  wire::reset();
  const int before = tls_fixture::outstandingMethods;
  {
    freeink::SecureClient client;
    client.setCACert("fixture");
    EXPECT_EQ(client.connect("example.test", 443), 0);
  }
  EXPECT_EQ(tls_fixture::outstandingMethods, before);
}
TEST(SecureClientLifecycle, VerifiedConnectionReleasesMethod) {
  wire::reset();
  wire::replies.push_back("fixture");
  const int before = tls_fixture::outstandingMethods;
  tls_fixture::verifyMode = 0;
  tls_fixture::domainChecked = false;
  {
    freeink::SecureClient client;
    client.setCACert("fixture");
    EXPECT_EQ(client.connect("example.test", 443), 1);
    EXPECT_EQ(tls_fixture::verifyMode, WOLFSSL_VERIFY_PEER);
    EXPECT_TRUE(tls_fixture::domainChecked);
  }
  EXPECT_EQ(tls_fixture::outstandingMethods, before);
}
