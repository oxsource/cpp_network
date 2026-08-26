#include "http/request.h"

#include <cctype>
#include <cstring>

namespace cpp_network {
namespace http {

namespace {

bool ContainsCrlf(const std::string& s) {
  return s.find('\r') != std::string::npos || s.find('\n') != std::string::npos;
}

bool LooksLikeAbsoluteUrl(const std::string& url) {
  // Requires a scheme (http:// or https://).
  return url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
}

}  // namespace

std::optional<std::string> Request::GetHeader(const std::string& name) const {
  for (const auto& [key, value] : headers_) {
    if (key.size() == name.size()) {
      bool eq = true;
      for (std::size_t i = 0; i < name.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(key[i])) !=
            std::tolower(static_cast<unsigned char>(name[i]))) {
          eq = false;
          break;
        }
      }
      if (eq) return value;
    }
  }
  return std::nullopt;
}

Request::Builder& Request::Builder::Body(const std::string& body) {
  body_ = body;
  has_body_ = true;
  // Default Content-Type: text/plain if not already set.
  bool has_ct = false;
  for (const auto& [name, value] : headers_) {
    (void)value;
    if (name == "Content-Type" || name == "content-type") {
      has_ct = true;
      break;
    }
  }
  if (!has_ct) {
    headers_.emplace_back("Content-Type", "text/plain");
  }
  return *this;
}

Request::Builder& Request::Builder::JsonBody(const std::string& json) {
  body_ = json;
  has_body_ = true;
  // Set or override Content-Type: application/json.
  for (auto& [name, value] : headers_) {
    if (name == "Content-Type" || name == "content-type") {
      value = "application/json";
      return *this;
    }
  }
  headers_.emplace_back("Content-Type", "application/json");
  return *this;
}

Result<Request> Request::Builder::Build() const {
  if (!LooksLikeAbsoluteUrl(url_)) {
    return Result<Request>::Err(
        Error(ErrorCode::kInvalidArgument, "URL must be absolute (http:// or https://)"));
  }
  if (ContainsCrlf(url_)) {
    return Result<Request>::Err(
        Error(ErrorCode::kInvalidArgument, "URL must not contain CRLF"));
  }
  for (const auto& [name, value] : headers_) {
    if (ContainsCrlf(name) || ContainsCrlf(value)) {
      return Result<Request>::Err(
          Error(ErrorCode::kInvalidArgument, "headers must not contain CRLF"));
    }
  }
  if (has_body_ &&
      (method_ == Method::kGet || method_ == Method::kHead ||
       method_ == Method::kOptions)) {
    return Result<Request>::Err(
        Error(ErrorCode::kInvalidArgument, "GET/HEAD/OPTIONS must not carry a body"));
  }
  if (timeout_.has_value() && timeout_->count() < 0) {
    return Result<Request>::Err(
        Error(ErrorCode::kInvalidArgument, "timeout must be non-negative"));
  }

  Request req(method_, url_, headers_, body_, has_body_, timeout_);
  return Result<Request>::Ok(std::move(req));
}

}  // namespace http
}  // namespace cpp_network
