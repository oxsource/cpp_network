#include "http/options.h"

#include <cstring>

namespace cpp_network {
namespace http {

namespace {

bool ContainsCrlf(const std::string& s) {
  return s.find('\r') != std::string::npos || s.find('\n') != std::string::npos;
}

}  // namespace

Result<void> Options::Validate() const {
  if (connect_timeout_.count() < 0 || read_timeout_.count() < 0 ||
      write_timeout_.count() < 0 || total_timeout_.count() < 0) {
    return Result<void>::Err(
        Error(ErrorCode::kInvalidArgument, "timeouts must be non-negative"));
  }
  if (max_redirects_ < 0) {
    return Result<void>::Err(
        Error(ErrorCode::kInvalidArgument, "max_redirects must be non-negative"));
  }
  if (interface_.has_value() && ContainsCrlf(*interface_)) {
    return Result<void>::Err(
        Error(ErrorCode::kInvalidArgument, "interface must not contain CRLF"));
  }
  if (local_address_.has_value() && ContainsCrlf(*local_address_)) {
    return Result<void>::Err(
        Error(ErrorCode::kInvalidArgument, "local address must not contain CRLF"));
  }
  if (local_port_.has_value() &&
      (*local_port_ < 0 || *local_port_ > 65535)) {
    return Result<void>::Err(
        Error(ErrorCode::kInvalidArgument, "local port out of range"));
  }
  if (proxy_.has_value() &&
      (proxy_->host.empty() || ContainsCrlf(proxy_->host) ||
       proxy_->port == 0)) {
    return Result<void>::Err(
        Error(ErrorCode::kInvalidArgument, "invalid proxy host/port"));
  }
  if (max_connections_per_host_ < 1) {
    return Result<void>::Err(Error(
        ErrorCode::kInvalidArgument, "max_connections_per_host must be >= 1"));
  }
  if (keep_alive_.count() < 0) {
    return Result<void>::Err(
        Error(ErrorCode::kInvalidArgument, "keep_alive must be non-negative"));
  }
  return Result<void>::Ok(Error());
}

}  // namespace http
}  // namespace cpp_network
