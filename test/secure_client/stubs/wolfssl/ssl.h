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
struct WOLFSSL_CTX {
  WOLFSSL_METHOD* method;
};
struct WOLFSSL {
  int error = 0;
  WOLFSSL_ALERT_HISTORY alerts;
};
namespace tls_fixture {
inline int outstandingMethods = 0, verifyMode = 0;
inline bool domainChecked = false;
inline int autoMethodCalls = 0, tls12MethodCalls = 0, connectCalls = 0;
inline int caLoadResult = 1;
inline std::deque<int> connectErrors;
inline std::deque<WOLFSSL_ALERT_HISTORY> connectAlerts;
inline void resetScripts() {
  autoMethodCalls = 0;
  tls12MethodCalls = 0;
  connectCalls = 0;
  caLoadResult = 1;
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
inline WOLFSSL_CTX* wolfSSL_CTX_new(WOLFSSL_METHOD* m) { return new WOLFSSL_CTX{m}; }
inline void wolfSSL_CTX_free(WOLFSSL_CTX* c) {
  delete c->method;
  delete c;
  --tls_fixture::outstandingMethods;
}
inline void wolfSSL_CTX_set_verify(WOLFSSL_CTX*, int mode, void*) { tls_fixture::verifyMode = mode; }
inline int wolfSSL_CTX_load_verify_buffer(WOLFSSL_CTX*, const unsigned char*, size_t, int) {
  return tls_fixture::caLoadResult;
}
inline void wolfSSL_SetIORecv(WOLFSSL_CTX*, int (*)(WOLFSSL*, char*, int, void*)) {}
inline void wolfSSL_SetIOSend(WOLFSSL_CTX*, int (*)(WOLFSSL*, char*, int, void*)) {}
inline WOLFSSL* wolfSSL_new(WOLFSSL_CTX*) { return new WOLFSSL; }
inline void wolfSSL_free(WOLFSSL* s) { delete s; }
inline int wolfSSL_check_domain_name(WOLFSSL*, const char*) {
  tls_fixture::domainChecked = true;
  return WOLFSSL_SUCCESS;
}
inline void wolfSSL_SetIOReadCtx(WOLFSSL*, void*) {}
inline void wolfSSL_SetIOWriteCtx(WOLFSSL*, void*) {}
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
inline int wolfSSL_write(WOLFSSL*, const void*, int n) { return n; }
inline int wolfSSL_read(WOLFSSL*, void*, int) { return 0; }
inline int wolfSSL_pending(WOLFSSL*) { return 0; }
