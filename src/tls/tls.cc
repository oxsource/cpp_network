#include "http/tls.h"

#include <string>

namespace cpp_network {
namespace http {

namespace {

constexpr const char* kBeginMarker = "-----BEGIN";
constexpr const char* kEndMarker = "-----END";

bool HasMatchingEnd(const std::string& value) {
  return value.find(kEndMarker) != std::string::npos;
}

bool ContainsCrlf(const std::string& s) {
  return s.find('\r') != std::string::npos || s.find('\n') != std::string::npos;
}

Result<void> Invalid(const std::string& message) {
  return Result<void>::Err(Error(ErrorCode::kInvalidArgument, message));
}

}  // namespace

// Inline PEM material (as opposed to a file path) is detected by the presence
// of the "-----BEGIN" marker; it must also carry a matching "-----END".
Result<void> Tls::Validate() const {
  if (ca_file_.has_value() && ca_pem_.has_value()) {
    return Invalid("ca_file and ca_certificate are mutually exclusive");
  }
  if (ca_pem_.has_value()) {
    if (!Tls::IsPemText(*ca_pem_) || !HasMatchingEnd(*ca_pem_)) {
      return Invalid("ca_certificate does not look like valid PEM");
    }
  } else if (ca_file_.has_value() && ca_file_->empty()) {
    return Invalid("ca_file must not be empty");
  }
  if (client_cert_.has_value() != client_key_.has_value()) {
    return Invalid(
        "client certificate and key must be configured together (mTLS)");
  }
  if (client_cert_.has_value()) {
    const bool cert_is_pem = Tls::IsPemText(*client_cert_);
    const bool key_is_pem = Tls::IsPemText(*client_key_);
    if (client_cert_->empty() || client_key_->empty()) {
      return Invalid("client certificate/key must not be empty");
    }
    if (cert_is_pem != key_is_pem) {
      return Invalid(
          "client certificate and key must both be inline PEM or both file "
          "paths");
    }
    if (cert_is_pem && (!HasMatchingEnd(*client_cert_) ||
                        !HasMatchingEnd(*client_key_))) {
      return Invalid("client certificate/key does not look like valid PEM");
    }
  }
  if (sni_.has_value()) {
    if (sni_->empty()) {
      return Invalid("sni must not be empty");
    }
    if (ContainsCrlf(*sni_)) {
      return Invalid("sni must not contain CRLF");
    }
  }
  return Result<void>::Ok(Error());
}

// static
bool Tls::IsPemText(const std::string& value) {
  return value.find(kBeginMarker) != std::string::npos;
}

}  // namespace http
}  // namespace cpp_network
