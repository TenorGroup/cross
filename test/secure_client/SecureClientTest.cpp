#include <gtest/gtest.h>
#include <wolfssl/ssl.h>
#include <SecureHttpClient.h>

#include <SecureClient.h>

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

template <typename Http>
void enableContextReuse(Http& http) {
  if constexpr (requires { http.setReuseTlsContext(true); }) http.setReuseTlsContext(true);
}

template <typename Http>
bool peerCloseComplete(const Http& http) {
  if constexpr (requires { http.peerCloseComplete(); }) return http.peerCloseComplete();
  return false;
}

template <typename Client>
bool hasTlsContext(const Client& client) {
  if constexpr (requires { client.hasTlsContext(); }) return client.hasTlsContext();
  return false;
}

TEST(SecureClientLifecycle, SeventyRangeConnectionsKeepOnlyOneTrustContextAndNoTcpTail) {
  resetFixture();
  wire::modelTimeWait = true;
  const int before = tls_fixture::outstandingMethods + tls_fixture::outstandingContexts +
                     tls_fixture::outstandingSessions + tls_fixture::outstandingCas + wire::livePcbs;
  int afterFirst = -1;
  {
    freeink::SecureHttpClient http;
    http.setCACert("fixture");
    http.setReuse(false);
    enableContextReuse(http);
    for (int part = 0; part < 70; ++part) {
      wire::replies.push_back("HTTP/1.1 206 Partial Content\r\nContent-Length: 3\r\n\r\nabc");
      ASSERT_TRUE(http.begin("https://example.test/font.cpfontpack"));
      http.addHeader("Range", "bytes=0-2");
      ASSERT_EQ(http.GET([](const uint8_t*, size_t) { return true; }), 206);
      ASSERT_TRUE(http.responseComplete());
      EXPECT_TRUE(peerCloseComplete(http));
      const int live = tls_fixture::outstandingMethods + tls_fixture::outstandingContexts +
                       tls_fixture::outstandingSessions + tls_fixture::outstandingCas + wire::livePcbs;
      if (part == 0) afterFirst = live;
      EXPECT_EQ(live, afterFirst) << "part=" << part + 1;
    }
    EXPECT_EQ(tls_fixture::caLoadCalls, 1);
    EXPECT_EQ(tls_fixture::domainCheckCalls, 70);
    EXPECT_EQ(tls_fixture::verifyMode, WOLFSSL_VERIFY_PEER);
    EXPECT_EQ(tls_fixture::cacheMode, WOLFSSL_SESS_CACHE_OFF);
    EXPECT_EQ(wire::connectAttempts, 70);
    EXPECT_EQ(wire::livePcbs, 0);
    EXPECT_EQ(tls_fixture::outstandingSessions, 0);
  }
  const int after = tls_fixture::outstandingMethods + tls_fixture::outstandingContexts +
                    tls_fixture::outstandingSessions + tls_fixture::outstandingCas + wire::livePcbs;
  EXPECT_EQ(after, before);
  std::cout << "RANGE_LIFETIME loops=70 ca_loads=" << tls_fixture::caLoadCalls
            << " live_growth=" << after - before << " tcp_tail=" << wire::livePcbs << '\n';
  wire::reset();
}

TEST(SecureClientLifecycle, CachedTrustSurvivesFailedHandshakeAndIsReleasedAtScopeExit) {
  resetFixture();
  {
    freeink::SecureClient client;
    enableContextReuse(client);
    client.setCACert("fixture");
    wire::replies.push_back("fixture");
    scriptFailure(MEMORY_ERROR);
    EXPECT_EQ(client.connect("example.test", 443), 0);
    EXPECT_EQ(tls_fixture::outstandingSessions, 0);
    EXPECT_TRUE(hasTlsContext(client));
    wire::replies.push_back("fixture");
    EXPECT_EQ(client.connect("example.test", 443), 1);
    client.stop();
    EXPECT_EQ(tls_fixture::caLoadCalls, 1);
    EXPECT_EQ(tls_fixture::outstandingSessions, 0);
    EXPECT_EQ(tls_fixture::outstandingCas, 1);
  }
  EXPECT_EQ(tls_fixture::outstandingCas, 0);
  EXPECT_EQ(tls_fixture::outstandingContexts, 0);
  EXPECT_EQ(tls_fixture::outstandingMethods, 0);
}

TEST(SecureClientTrust, CachedContextRejectsBadCaAndReloadsOnTrustChange) {
  resetFixture();
  {
    freeink::SecureClient client;
    enableContextReuse(client);
    client.setCACert("fixture");
    tls_fixture::caLoadResult = WOLFSSL_FAILURE;
    wire::replies.push_back("fixture");
    EXPECT_EQ(client.connect("example.test", 443), 0);
    EXPECT_EQ(tls_fixture::outstandingContexts, 0);
    EXPECT_FALSE(hasTlsContext(client));
    tls_fixture::caLoadResult = WOLFSSL_SUCCESS;
    wire::replies.push_back("fixture");
    EXPECT_EQ(client.connect("example.test", 443), 1);
    client.setCACert("other-ca");
    EXPECT_EQ(tls_fixture::outstandingCas, 0);
    wire::replies.push_back("fixture");
    EXPECT_EQ(client.connect("example.test", 443), 1);
    EXPECT_EQ(tls_fixture::caLoadCalls, 3);
  }
  EXPECT_EQ(tls_fixture::outstandingCas, 0);
}

TEST(SecureClientLifecycle, CachedTls12FallbackDoesNotRecreateCaOnNextPart) {
  resetFixture();
  {
    freeink::SecureClient client;
    enableContextReuse(client);
    client.setCACert("fixture");
    wire::replies.push_back("fixture");
    wire::replies.push_back("fixture");
    scriptFailure(VERSION_ERROR);
    EXPECT_EQ(client.connect("example.test", 443), 1);
    EXPECT_EQ(tls_fixture::caLoadCalls, 2);
    client.stop();
    wire::replies.push_back("fixture");
    EXPECT_EQ(client.connect("example.test", 443), 1);
    EXPECT_EQ(tls_fixture::caLoadCalls, 2);
    EXPECT_EQ(tls_fixture::autoMethodCalls, 1);
    EXPECT_EQ(tls_fixture::tls12MethodCalls, 1);
  }
  EXPECT_EQ(tls_fixture::outstandingContexts, 0);
}

TEST(SecureClientCancellation, CancelWhileWaitingForPeerCloseReleasesCachedTrust) {
  resetFixture();
  {
    freeink::SecureHttpClient http;
    http.setCACert("fixture");
    http.setReuse(false);
    enableContextReuse(http);
    wire::replies.push_back("HTTP/1.1 206 Partial Content\r\nContent-Length: 3\r\n\r\nabc");
    ASSERT_TRUE(http.begin("https://example.test/font.cpfontpack"));
    bool bodyDone = false;
    EXPECT_EQ(http.GET([&](const uint8_t*, size_t) { bodyDone = true; return true; },
                       [&] { return bodyDone; }), 206);
    EXPECT_TRUE(http.aborted());
    EXPECT_FALSE(peerCloseComplete(http));
    EXPECT_EQ(tls_fixture::outstandingSessions, 0);
  }
  EXPECT_EQ(tls_fixture::outstandingContexts, 0);
  EXPECT_EQ(tls_fixture::outstandingCas, 0);
}

TEST(SecureClientLifecycle, PeerIgnoringConnectionCloseHasBoundedWait) {
  resetFixture();
  {
    freeink::SecureHttpClient http;
    http.setCACert("fixture");
    http.setReuse(false);
    enableContextReuse(http);
    wire::replies.push_back("HTTP/1.1 206 Partial Content\r\nContent-Length: 3\r\n\r\nabc");
    ASSERT_TRUE(http.begin("https://example.test/font.cpfontpack"));
    const auto before = millis();
    EXPECT_EQ(http.GET([](const uint8_t*, size_t) { return true; }), 206);
    EXPECT_TRUE(http.responseComplete());
    EXPECT_FALSE(http.aborted());
    EXPECT_FALSE(peerCloseComplete(http));
    EXPECT_LT(millis() - before, 1100UL);
  }
  EXPECT_EQ(tls_fixture::outstandingContexts, 0);
}

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
