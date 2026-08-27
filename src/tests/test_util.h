#ifndef CPP_NETWORK_TESTS_TEST_UTIL_H_
#define CPP_NETWORK_TESTS_TEST_UTIL_H_

#include <cstdlib>
#include <string>

namespace cpp_network {
namespace http {

// Root directory that holds test assets (certificates). Tests read assets
// through this helper so the exact same binaries run both on the host
// (default repo-relative layout) and on an Android device, where make push /
// make run export NETLIB_TEST_DATA_DIR=/data/local/tmp/cpp_network/certs
// (specs/004 contracts/device-test-contract.md).
inline std::string TestAssetRoot() {
  const char* root = std::getenv("NETLIB_TEST_DATA_DIR");
  if (root != nullptr && *root != '\0') {
    return std::string(root);
  }
  return std::string("src/tests");
}

inline std::string CertPath(const std::string& name) {
  return TestAssetRoot() + "/certs/" + name;
}

// Service base URLs. Defaults match the local fixtures launched by the host
// gtest suites / make run; each can be overridden so the same binary targets
// forwarded ports or alternate hosts.
inline std::string HttpBase() {
  const char* v = std::getenv("NETLIB_TEST_HTTP_BASE");
  return (v != nullptr && *v != '\0') ? std::string(v)
                                      : std::string("http://127.0.0.1:18080");
}

inline std::string TlsBase() {
  const char* v = std::getenv("NETLIB_TEST_HTTPS_BASE");
  return (v != nullptr && *v != '\0')
             ? std::string(v)
             : std::string("https://127.0.0.1:18443");
}

inline std::string MtlsBase() {
  const char* v = std::getenv("NETLIB_TEST_MTLS_BASE");
  return (v != nullptr && *v != '\0')
             ? std::string(v)
             : std::string("https://127.0.0.1:18444");
}

}  // namespace http
}  // namespace cpp_network

#endif  // CPP_NETWORK_TESTS_TEST_UTIL_H_
