#include <gtest/gtest.h>
#include <wolfssl/ssl.h>

#include "../../freeink-sdk/libs/network/SecureNet/include/SecureClient.h"

namespace {
void resetFixture() {
  wire::reset();
  tls_fixture::resetScripts();
}

void scriptFailure(int error, int alertCode = -1) {
  tls_fixture::connectErrors.push_back(error);
  if (alertCode >= 0) {
    WOLFSSL_ALERT_HISTORY history{};
    history.last_rx.code = alertCode;
    history.last_rx.level = alert_fatal;
    tls_fixture::connectAlerts.push_back(history);
  }
}

void expectOneAttempt(int error, int alertCode = -1) {
  resetFixture();
  wire::replies.push_back("fixture");
  scriptFailure(error, alertCode);
  const int before = tls_fixture::outstandingMethods;
  {
    freeink::SecureClient client;
    client.setCACert("fixture");
    EXPECT_EQ(client.connect("example.test", 443), 0);
  }
  EXPECT_EQ(wire::requests.size(), 1u);
  EXPECT_EQ(wire::connectAttempts, 1);
  EXPECT_EQ(tls_fixture::autoMethodCalls, 1);
  EXPECT_EQ(tls_fixture::tls12MethodCalls, 0);
  EXPECT_EQ(tls_fixture::connectCalls, 1);
  EXPECT_EQ(tls_fixture::outstandingMethods, before);
}
}  // namespace

TEST(SecureClientLifecycle, MissingCaFailsBeforeTransportOrAllocation) {
  resetFixture();
  const int before = tls_fixture::outstandingMethods;
  {
    freeink::SecureClient client;
    EXPECT_EQ(client.connect("example.test", 443), 0);
  }
  EXPECT_TRUE(wire::requests.empty());
  EXPECT_EQ(wire::connectAttempts, 0);
  EXPECT_EQ(tls_fixture::autoMethodCalls, 0);
  EXPECT_EQ(tls_fixture::tls12MethodCalls, 0);
  EXPECT_EQ(tls_fixture::connectCalls, 0);
  EXPECT_EQ(tls_fixture::outstandingMethods, before);
}

TEST(SecureClientLifecycle, TcpFailureDoesNotLeakMethod) {
  resetFixture();
  const int before = tls_fixture::outstandingMethods;
  {
    freeink::SecureClient client;
    client.setCACert("fixture");
    EXPECT_EQ(client.connect("example.test", 443), 0);
  }
  EXPECT_EQ(tls_fixture::outstandingMethods, before);
  EXPECT_EQ(wire::connectAttempts, 1);
}

TEST(SecureClientLifecycle, CertificateFailureOverridesReceivedProtocolAlert) {
  expectOneAttempt(VERIFY_CERT_ERROR, protocol_version);
}

TEST(SecureClientLifecycle, UnknownFailureOverridesReceivedProtocolAlert) {
  expectOneAttempt(-999, protocol_version);
}

TEST(SecureClientLifecycle, MemoryFailureOverridesReceivedProtocolAlert) {
  expectOneAttempt(MEMORY_ERROR, protocol_version);
}

TEST(SecureClientLifecycle, CertificateFailureDoesNotFallback) {
  expectOneAttempt(VERIFY_CERT_ERROR, certificate_unknown);
}

TEST(SecureClientLifecycle, HostnameFailureDoesNotFallback) {
  expectOneAttempt(DOMAIN_NAME_MISMATCH);
}

TEST(SecureClientLifecycle, ExpiredCertificateDoesNotFallback) {
  expectOneAttempt(VERIFY_CERT_ERROR, certificate_expired);
}

TEST(SecureClientLifecycle, CaFailureDoesNotFallback) {
  expectOneAttempt(VERIFY_CERT_ERROR, unknown_ca);
}

TEST(SecureClientLifecycle, CaLoadFailureDoesNotFallback) {
  resetFixture();
  wire::replies.push_back("fixture");
  tls_fixture::caLoadResult = WOLFSSL_FAILURE;
  const int before = tls_fixture::outstandingMethods;
  {
    freeink::SecureClient client;
    client.setCACert("fixture");
    EXPECT_EQ(client.connect("example.test", 443), 0);
  }
  EXPECT_EQ(wire::requests.size(), 1u);
  EXPECT_EQ(wire::connectAttempts, 1);
  EXPECT_EQ(tls_fixture::autoMethodCalls, 1);
  EXPECT_EQ(tls_fixture::tls12MethodCalls, 0);
  EXPECT_EQ(tls_fixture::connectCalls, 0);
  EXPECT_EQ(tls_fixture::outstandingMethods, before);
}

TEST(SecureClientTrust, HostnameCheckFailureStopsBeforeTls) {
  resetFixture();
  wire::replies.push_back("fixture");
  tls_fixture::domainCheckResult = WOLFSSL_FAILURE;
  const int before = tls_fixture::outstandingMethods;
  {
    freeink::SecureClient client;
    client.setCACert("fixture");
    EXPECT_EQ(client.connect("wrong-name.test", 443), 0);
  }
  EXPECT_TRUE(tls_fixture::domainChecked);
  EXPECT_EQ(wire::connectAttempts, 1);
  EXPECT_EQ(tls_fixture::connectCalls, 0);
  EXPECT_EQ(tls_fixture::outstandingMethods, before);
}

TEST(SecureClientTrust, InsecureConnectionSkipsHostnameCheck) {
  resetFixture();
  wire::replies.push_back("fixture");
  tls_fixture::domainCheckResult = WOLFSSL_FAILURE;
  {
    freeink::SecureClient client;
    client.setInsecure();
    EXPECT_EQ(client.connect("internal.test", 443), 1);
    EXPECT_EQ(tls_fixture::verifyMode, WOLFSSL_VERIFY_NONE);
  }
  EXPECT_FALSE(tls_fixture::domainChecked);
  EXPECT_EQ(tls_fixture::connectCalls, 1);
}

TEST(SecureClientLifecycle, MemoryFailureDoesNotFallback) {
  expectOneAttempt(MEMORY_ERROR);
}

TEST(SecureClientLifecycle, UnknownFailureDoesNotFallback) {
  expectOneAttempt(-999);
}

TEST(SecureClientLifecycle, GenericFatalAlertDoesNotFallback) {
  expectOneAttempt(FATAL_ERROR);
}

TEST(SecureClientLifecycle, VersionFailureFallsBackOnce) {
  resetFixture();
  wire::replies.push_back("first");
  wire::replies.push_back("second");
  scriptFailure(VERSION_ERROR);
  const int before = tls_fixture::outstandingMethods;
  {
    freeink::SecureClient client;
    client.setCACert("fixture");
    EXPECT_EQ(client.connect("example.test", 443), 1);
  }
  EXPECT_EQ(wire::requests.size(), 2u);
  EXPECT_EQ(wire::connectAttempts, 2);
  EXPECT_EQ(tls_fixture::autoMethodCalls, 1);
  EXPECT_EQ(tls_fixture::tls12MethodCalls, 1);
  EXPECT_EQ(tls_fixture::connectCalls, 2);
  EXPECT_EQ(tls_fixture::outstandingMethods, before);
}

TEST(SecureClientLifecycle, ProtocolVersionAlertFallsBackOnce) {
  resetFixture();
  wire::replies.push_back("first");
  wire::replies.push_back("second");
  scriptFailure(FATAL_ERROR, protocol_version);
  {
    freeink::SecureClient client;
    client.setCACert("fixture");
    EXPECT_EQ(client.connect("example.test", 443), 1);
  }
  EXPECT_EQ(wire::requests.size(), 2u);
  EXPECT_EQ(wire::connectAttempts, 2);
  EXPECT_EQ(tls_fixture::tls12MethodCalls, 1);
}

TEST(SecureClientLifecycle, TransportFailureFallsBackOnce) {
  resetFixture();
  wire::replies.push_back("first");
  wire::replies.push_back("second");
  scriptFailure(SOCKET_ERROR_E);
  {
    freeink::SecureClient client;
    client.setCACert("fixture");
    EXPECT_EQ(client.connect("example.test", 443), 1);
  }
  EXPECT_EQ(wire::requests.size(), 2u);
  EXPECT_EQ(wire::connectAttempts, 2);
  EXPECT_EQ(tls_fixture::tls12MethodCalls, 1);
}

TEST(SecureClientLifecycle, VerifiedConnectionReleasesMethod) {
  resetFixture();
  wire::replies.push_back("fixture");
  const int before = tls_fixture::outstandingMethods;
  {
    freeink::SecureClient client;
    client.setCACert("fixture");
    EXPECT_EQ(client.connect("example.test", 443), 1);
    EXPECT_EQ(tls_fixture::verifyMode, WOLFSSL_VERIFY_PEER);
    EXPECT_TRUE(tls_fixture::domainChecked);
  }
  EXPECT_EQ(wire::requests.size(), 1u);
  EXPECT_EQ(wire::connectAttempts, 1);
  EXPECT_EQ(tls_fixture::autoMethodCalls, 1);
  EXPECT_EQ(tls_fixture::tls12MethodCalls, 0);
  EXPECT_EQ(tls_fixture::connectCalls, 1);
  EXPECT_EQ(tls_fixture::outstandingMethods, before);
}

TEST(SecureClientLifecycle, PeerCloseRetriesOnlyOnceAndPreservesTrustFailure) {
  resetFixture();
  wire::replies = {"first", "second", "must-not-connect"};
  scriptFailure(SOCKET_PEER_CLOSED_E);
  scriptFailure(VERIFY_CERT_ERROR);
  const int before = tls_fixture::outstandingMethods;
  {
    freeink::SecureClient client;
    client.setCACert("fixture");
    EXPECT_EQ(client.connect("example.test", 443), 0);
  }
  EXPECT_EQ(wire::connectAttempts, 2);
  EXPECT_EQ(tls_fixture::connectCalls, 2);
  EXPECT_EQ(wire::replies.size(), 1u);
  EXPECT_EQ(tls_fixture::outstandingMethods, before);
}

TEST(SecureClientLifecycle, StaleAlertBlocksOtherwiseRetryableTransportFailure) {
  expectOneAttempt(SOCKET_ERROR_E, protocol_version);
}

TEST(SecureClientLifecycle, WarningProtocolAlertCannotAuthorizeFallback) {
  resetFixture();
  wire::replies = {"first", "must-not-connect"};
  scriptFailure(FATAL_ERROR, protocol_version);
  tls_fixture::connectAlerts.front().last_rx.level = 1;
  {
    freeink::SecureClient client;
    client.setCACert("fixture");
    EXPECT_EQ(client.connect("example.test", 443), 0);
  }
  EXPECT_EQ(wire::connectAttempts, 1);
  EXPECT_EQ(wire::replies.size(), 1u);
}

namespace {
template <class T, class Callback>
void installAbortCallback(T& client, Callback callback) {
  // Keep the regression runnable against the pre-fix class too.
  if constexpr (requires { client.setAbortCallback(callback); }) client.setAbortCallback(callback);
}
}

TEST(SecureClientCancellation, StopsBeforeTcpWhenAlreadyCancelled) {
  resetFixture();
  wire::replies.push_back("fixture");
  freeink::SecureClient client;
  client.setCACert("fixture");
  installAbortCallback(client, [] { return true; });
  EXPECT_EQ(client.connect("example.test", 443), 0);
  EXPECT_EQ(wire::connectAttempts, 0);
}

TEST(SecureClientCancellation, PollsWhileTlsPeerStallsAndReclaimsSession) {
  resetFixture();
  wire::replies.push_back("fixture");
  for (int i = 0; i < 100; ++i) scriptFailure(WOLFSSL_ERROR_WANT_READ);
  const int before = tls_fixture::outstandingMethods;
  int pumps = 0;
  {
    freeink::SecureClient client;
    client.setCACert("fixture");
    installAbortCallback(client, [&] { return ++pumps >= 4; });
    EXPECT_EQ(client.connect("example.test", 443), 0);
    EXPECT_GE(pumps, 4);
    EXPECT_LE(tls_fixture::connectCalls, 3);
    EXPECT_EQ(tls_fixture::tls12MethodCalls, 0);
  }
  EXPECT_EQ(tls_fixture::outstandingMethods, before);
}

TEST(SecureClientCancellation, CancellationPreventsTlsFallback) {
  resetFixture();
  wire::replies = {"first", "second"};
  scriptFailure(VERSION_ERROR);
  freeink::SecureClient client;
  client.setCACert("fixture");
  installAbortCallback(client, [] { return tls_fixture::connectCalls > 0; });
  EXPECT_EQ(client.connect("example.test", 443), 0);
  EXPECT_EQ(wire::connectAttempts, 1);
  EXPECT_EQ(tls_fixture::tls12MethodCalls, 0);
}
