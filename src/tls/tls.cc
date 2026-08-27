#include "http/tls.h"

#include <fcntl.h>
#include <unistd.h>

#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <vector>

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

// static
const char* Tls::CachedPemPath(const std::string& pem) {
  static std::mutex mutex;
  static std::map<std::string, std::string> cache;
  std::lock_guard<std::mutex> lock(mutex);
  auto it = cache.find(pem);
  if (it != cache.end()) {
    return it->second.c_str();
  }

  const char* tmpdir = std::getenv("TMPDIR");
  std::string tmpl =
      (tmpdir != nullptr && tmpdir[0] != '\0' ? std::string(tmpdir)
                                              : std::string("/tmp")) +
      "/cpp_network_pem_XXXXXX";
  std::vector<char> buf(tmpl.begin(), tmpl.end());
  buf.push_back('\0');
  int fd = ::mkstemp(buf.data());
  if (fd < 0) {
    return nullptr;
  }
  ssize_t written = ::write(fd, pem.data(), pem.size());
  ::close(fd);
  if (written < 0 || static_cast<std::size_t>(written) != pem.size()) {
    ::unlink(buf.data());
    return nullptr;
  }
  // The map node keeps a stable copy; the returned pointer remains valid.
  return cache.emplace(pem, std::string(buf.data())).first->second.c_str();
}

}  // namespace http
}  // namespace cpp_network
