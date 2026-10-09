#pragma once
#include <cstddef>
#include <deque>

// Ownership and configuration doubles only. These tests do not validate TLS cryptography.
struct WOLFSSL_METHOD {};
struct WOLFSSL_ALERT {
  int code;
  int level;
};
struct WOLFSSL_ALERT_HISTORY {
  WOLFSSL_ALERT last_rx{-1, -1};
  WOLFSSL_ALERT last_tx{-1, -1};
};
struct WOLFSSL;
struct WOLFSSL_CTX {
  WOLFSSL_METHOD* method;
  unsigned char* ca = nullptr;
  int (*recv)(WOLFSSL*, char*, int, void*) = nullptr;
  int (*send)(WOLFSSL*, char*, int, void*) = nullptr;
};
struct WOLFSSL {
  int error = 0;
  WOLFSSL_ALERT_HISTORY alerts;
  WOLFSSL_CTX* ctx = nullptr;
  void* readCtx = nullptr;
  void* writeCtx = nullptr;
};
namespace tls_fixture {
inline int outstandingMethods = 0, verifyMode = 0;
inline int outstandingContexts = 0, outstandingSessions = 0, outstandingCas = 0, caLoadCalls = 0;
inline int domainCheckCalls = 0;
inline long cacheMode = -1;
inline bool domainChecked = false;
inline int autoMethodCalls = 0, tls12MethodCalls = 0, connectCalls = 0;
inline int caLoadResult = 1;
inline int domainCheckResult = 1;
inline std::deque<int> connectErrors;
inline std::deque<WOLFSSL_ALERT_HISTORY> connectAlerts;
inline void resetScripts() {
  autoMethodCalls = 0;
  tls12MethodCalls = 0;
  connectCalls = 0;
  caLoadCalls = 0;
  cacheMode = -1;
  domainCheckCalls = 0;
  caLoadResult = 1;
  domainCheckResult = 1;
  connectErrors.clear();
  connectAlerts.clear();
  verifyMode = 0;
  domainChecked = false;
}
}  // namespace tls_fixture
constexpr int WOLFSSL_SUCCESS = 1, WOLFSSL_FAILURE = 0, WOLFSSL_CBIO_ERR_CONN_CLOSE = -5,
              WOLFSSL_CBIO_ERR_WANT_WRITE = -2, WOLFSSL_CBIO_ERR_WANT_READ = -3;
constexpr int WOLFSSL_ERROR_NONE = 0, WOLFSSL_ERROR_WANT_READ = 2, WOLFSSL_ERROR_WANT_WRITE = 3,
              WOLFSSL_ERROR_ZERO_RETURN = 6;
constexpr int WOLFSSL_VERIFY_NONE = 0, WOLFSSL_VERIFY_PEER = 1, WOLFSSL_FILETYPE_PEM = 1, WOLFSSL_SNI_HOST_NAME = 0;
constexpr int WOLFSSL_FATAL_ERROR = -1, FATAL_ERROR = -313, MEMORY_ERROR = -303, VERIFY_CERT_ERROR = -329,
              DOMAIN_NAME_MISMATCH = -322, VERSION_ERROR = -326, SOCKET_ERROR_E = -308,
              SOCKET_PEER_CLOSED_E = -397;
constexpr int alert_fatal = 2, certificate_expired = 45, certificate_unknown = 46, unknown_ca = 48,
              protocol_version = 70, wolfssl_alert_protocol_version = protocol_version;
inline WOLFSSL_METHOD* wolfSSLv23_client_method() {
  ++tls_fixture::outstandingMethods;
  ++tls_fixture::autoMethodCalls;
  return new WOLFSSL_METHOD;
}
inline WOLFSSL_METHOD* wolfTLSv1_2_client_method() {
  ++tls_fixture::outstandingMethods;
  ++tls_fixture::tls12MethodCalls;
  return new WOLFSSL_METHOD;
}
inline WOLFSSL_CTX* wolfSSL_CTX_new(WOLFSSL_METHOD* m) {
  ++tls_fixture::outstandingContexts;
  return new WOLFSSL_CTX{m};
}
inline void wolfSSL_CTX_free(WOLFSSL_CTX* c) {
  if (c->ca) { delete[] c->ca; --tls_fixture::outstandingCas; }
  delete c->method;
  delete c;
  --tls_fixture::outstandingMethods;
  --tls_fixture::outstandingContexts;
}
inline void wolfSSL_CTX_set_verify(WOLFSSL_CTX*, int mode, void*) { tls_fixture::verifyMode = mode; }
inline int wolfSSL_CTX_load_verify_buffer(WOLFSSL_CTX* ctx, const unsigned char*, size_t, int) {
  ++tls_fixture::caLoadCalls;
  if (tls_fixture::caLoadResult == WOLFSSL_SUCCESS) {
    ctx->ca = new unsigned char[4096];
    ++tls_fixture::outstandingCas;
  }
  return tls_fixture::caLoadResult;
}
constexpr long WOLFSSL_SESS_CACHE_OFF = 0;
inline long wolfSSL_CTX_set_session_cache_mode(WOLFSSL_CTX*, long mode) {
  tls_fixture::cacheMode = mode;
  return WOLFSSL_SUCCESS;
}
inline void wolfSSL_SetIORecv(WOLFSSL_CTX* ctx, int (*callback)(WOLFSSL*, char*, int, void*)) { ctx->recv = callback; }
inline void wolfSSL_SetIOSend(WOLFSSL_CTX* ctx, int (*callback)(WOLFSSL*, char*, int, void*)) { ctx->send = callback; }
inline WOLFSSL* wolfSSL_new(WOLFSSL_CTX* ctx) {
  ++tls_fixture::outstandingSessions;
  auto* session = new WOLFSSL;
  session->ctx = ctx;
  return session;
}
inline void wolfSSL_free(WOLFSSL* session) { delete session; --tls_fixture::outstandingSessions; }
inline int wolfSSL_check_domain_name(WOLFSSL*, const char*) {
  ++tls_fixture::domainCheckCalls;
  tls_fixture::domainChecked = true;
  return tls_fixture::domainCheckResult;
}
inline void wolfSSL_SetIOReadCtx(WOLFSSL* session, void* ctx) { session->readCtx = ctx; }
inline void wolfSSL_SetIOWriteCtx(WOLFSSL* session, void* ctx) { session->writeCtx = ctx; }
inline int wolfSSL_UseSNI(WOLFSSL*, int, const char*, size_t) { return WOLFSSL_SUCCESS; }
inline int wolfSSL_connect(WOLFSSL* ssl) {
  ++tls_fixture::connectCalls;
  if (tls_fixture::connectErrors.empty()) return WOLFSSL_SUCCESS;
  ssl->error = tls_fixture::connectErrors.front();
  tls_fixture::connectErrors.pop_front();
  if (!tls_fixture::connectAlerts.empty()) {
    ssl->alerts = tls_fixture::connectAlerts.front();
    tls_fixture::connectAlerts.pop_front();
  }
  return WOLFSSL_FAILURE;
}
inline int wolfSSL_get_error(WOLFSSL* ssl, int) { return ssl->error; }
inline int wolfSSL_get_alert_history(WOLFSSL* ssl, WOLFSSL_ALERT_HISTORY* history) {
  *history = ssl->alerts;
  return WOLFSSL_SUCCESS;
}
inline const char* wolfSSL_get_version(WOLFSSL*) { return "fixture"; }
inline const char* wolfSSL_get_cipher(WOLFSSL*) { return "fixture"; }
inline int wolfSSL_write(WOLFSSL* session, const void* bytes, int count) {
  return session->ctx->send(session, const_cast<char*>(static_cast<const char*>(bytes)), count, session->writeCtx);
}
inline int wolfSSL_read(WOLFSSL* session, void* bytes, int count) {
  const int result = session->ctx->recv(session, static_cast<char*>(bytes), count, session->readCtx);
  if (result > 0) return result;
  session->error = result == WOLFSSL_CBIO_ERR_WANT_READ ? WOLFSSL_ERROR_WANT_READ : WOLFSSL_ERROR_ZERO_RETURN;
  return 0;
}
inline int wolfSSL_pending(WOLFSSL*) { return 0; }
