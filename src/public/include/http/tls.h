#ifndef CPP_NETWORK_HTTP_TLS_H_
#define CPP_NETWORK_HTTP_TLS_H_

#include <optional>
#include <string>

#include "http/export.h"
#include "http/result.h"

namespace cpp_network {
namespace http {

enum class CPP_NETWORK_HTTP_EXPORT VerifyMode {
  kVerifyPeer,
  kSkipVerification,
};

// TLS configuration: CA certificates (memory PEM or file path), client
// certificates for mTLS, SNI override, and verification mode.
class CPP_NETWORK_HTTP_EXPORT Tls {
 public:
  Tls() = default;

  VerifyMode verify_mode() const { return verify_mode_; }
  const std::optional<std::string>& ca_pem() const { return ca_pem_; }
  const std::optional<std::string>& ca_file() const { return ca_file_; }
  const std::optional<std::string>& client_cert() const { return client_cert_; }
  const std::optional<std::string>& client_key() const { return client_key_; }
  const std::optional<std::string>& sni() const { return sni_; }

  Tls& SetVerifyMode(VerifyMode mode) {
    verify_mode_ = mode;
    return *this;
  }
  Tls& SetCaCertificate(const std::string& pem) {
    ca_pem_ = pem;
    return *this;
  }
  Tls& SetCaFile(const std::string& path) {
    ca_file_ = path;
    return *this;
  }
  Tls& SetClientCertificate(const std::string& cert, const std::string& key) {
    client_cert_ = cert;
    client_key_ = key;
    return *this;
  }
  Tls& SetSni(const std::string& hostname) {
    sni_ = hostname;
    return *this;
  }

  // Validates the configuration: CA sources are mutually exclusive, client
  // certificate/key must be set together, SNI must not contain CRLF, and
  // inline PEM material must look well-formed.
  Result<void> Validate() const;

  // True if value carries inline PEM material ("-----BEGIN" marker) as
  // opposed to a file path. Shared by validation and the curl mapping layer
  // to decide between *_BLOB and path-based CURLOPTs.
  static bool IsInlinePem(const std::string& value);

  // Materializes inline PEM into a temp file (created under $TMPDIR or /tmp)
  // for libcurl builds whose runtime rejects *_BLOB options. Results are
  // cached per content for the process lifetime, so the returned pointer
  // stays valid across transfers. Returns nullptr on I/O failure.
  static const char* MaterializePem(const std::string& pem);

 private:
  VerifyMode verify_mode_ = VerifyMode::kVerifyPeer;
  std::optional<std::string> ca_pem_;
  std::optional<std::string> ca_file_;
  std::optional<std::string> client_cert_;
  std::optional<std::string> client_key_;
  std::optional<std::string> sni_;
};

}  // namespace http
}  // namespace cpp_network

#endif  // CPP_NETWORK_HTTP_TLS_H_
