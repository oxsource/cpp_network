#ifndef CPP_NETWORK_COMM_TLS_H_
#define CPP_NETWORK_COMM_TLS_H_

#include <optional>
#include <string>

#include "comm/export.h"
#include "comm/result.h"

namespace cpp_network {
namespace comm {

// How the server's TLS certificate is treated during handshake.
enum class CPP_NETWORK_HTTP_EXPORT VerifyMode {
  // Validate the server certificate chain against the system CA store (or
  // custom ca_pem/ca_file): chain of trust, hostname match, expiry. A bad
  // certificate fails the handshake with kCertificateVerificationFailed.
  kVerifyPeer,
  // Accept any certificate without validating it (self-signed, expired, or
  // forged are all accepted). Insecure; only use in tests or debugging.
  kSkipVerification,
};

// TLS configuration: CA certificates (inline PEM or file path), client
// certificates for mTLS, SNI override, and verification mode.
//
// Immutable value type: construct a fully configured instance via
// Tls::Builder (or use the default instance for verified-peer TLS); there
// are no mutators, so an instance cannot change after it exists.
class CPP_NETWORK_HTTP_EXPORT Tls {
 public:
  Tls() = default;

  VerifyMode verify_mode() const { return verify_mode_; }
  const std::optional<std::string>& ca_pem() const { return ca_pem_; }
  const std::optional<std::string>& ca_file() const { return ca_file_; }
  const std::optional<std::string>& client_cert() const { return client_cert_; }
  const std::optional<std::string>& client_key() const { return client_key_; }
  const std::optional<std::string>& sni() const { return sni_; }

  // Validates the configuration: CA sources are mutually exclusive, client
  // certificate/key must be set together, SNI must not contain CRLF, and
  // inline PEM material must look well-formed.
  Result<void> Validate() const;

  // True if value looks like inline PEM-encoded text (carries a
  // "-----BEGIN" marker) as opposed to a filesystem path. Shared by
  // validation and the curl mapping layer to decide between *_BLOB options
  // and path-based CURLOPTs.
  static bool IsPemText(const std::string& value);

  // Fluent builder producing an immutable Tls instance.
  //
  // Naming: SetCaPem stores CA certificate(s) as inline PEM text, SetCaFile
  // points at a PEM file on disk; the two are mutually exclusive (rejected by
  // Validate()). Client material passed to SetCertificate may be either
  // inline PEM or file paths; cert and key must always be supplied together.
  // "Client material" is implicit: certificates are always client-side, since
  // the server certificate arrives during the handshake itself.
  class Builder {
   public:
    Builder() = default;

    Builder& SetVerifyMode(VerifyMode mode) {
      verify_mode_ = mode;
      return *this;
    }
    Builder& SetCaPem(const std::string& pem) {
      ca_pem_ = pem;
      return *this;
    }
    Builder& SetCaFile(const std::string& path) {
      ca_file_ = path;
      return *this;
    }
    Builder& SetCertificate(const std::string& cert, const std::string& key) {
      client_cert_ = cert;
      client_key_ = key;
      return *this;
    }
    Builder& SetSni(const std::string& hostname) {
      sni_ = hostname;
      return *this;
    }

    Tls Build() const {
      return Tls(verify_mode_, ca_pem_, ca_file_, client_cert_, client_key_,
                 sni_);
    }

   private:
    VerifyMode verify_mode_ = VerifyMode::kVerifyPeer;
    std::optional<std::string> ca_pem_;
    std::optional<std::string> ca_file_;
    std::optional<std::string> client_cert_;
    std::optional<std::string> client_key_;
    std::optional<std::string> sni_;
  };

 private:
  friend class Builder;

  Tls(VerifyMode mode, std::optional<std::string> ca_pem,
      std::optional<std::string> ca_file,
      std::optional<std::string> client_cert,
      std::optional<std::string> client_key, std::optional<std::string> sni)
      : verify_mode_(mode),
        ca_pem_(std::move(ca_pem)),
        ca_file_(std::move(ca_file)),
        client_cert_(std::move(client_cert)),
        client_key_(std::move(client_key)),
        sni_(std::move(sni)) {}

  VerifyMode verify_mode_ = VerifyMode::kVerifyPeer;
  std::optional<std::string> ca_pem_;
  std::optional<std::string> ca_file_;
  std::optional<std::string> client_cert_;
  std::optional<std::string> client_key_;
  std::optional<std::string> sni_;
};

}  // namespace comm
}  // namespace cpp_network

#endif  // CPP_NETWORK_COMM_TLS_H_
