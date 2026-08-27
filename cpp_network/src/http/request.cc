#include "http/request.h"

#include "detail/http_constants.h"


#include "comm/error.h"
#include "comm/result.h"

#include "comm/tls.h"
#include "comm/options.h"
#include "comm/url.h"
namespace cpp_network {
namespace http {

// Shared core types (canonical home: cpp_network::comm).
using cpp_network::comm::Error;
using cpp_network::comm::ErrorCode;
using cpp_network::comm::ErrorCodeToString;
using cpp_network::comm::Result;
using cpp_network::comm::Tls;
using cpp_network::comm::VerifyMode;
using cpp_network::comm::Options;
using cpp_network::comm::Proxy;
using cpp_network::comm::Url;




using detail::HttpConstants;

namespace {

bool ContainsCrlf(const std::string& s) {
  return s.find('\r') != std::string::npos || s.find('\n') != std::string::npos;
}

bool LooksLikeAbsoluteUrl(const std::string& url) {
  // Requires a scheme (http:// or https://).
  return url.rfind(HttpConstants::kHttpScheme, 0) == 0 ||
         url.rfind(HttpConstants::kHttpsScheme, 0) == 0;
}

}  // namespace

std::optional<std::string> Request::GetHeader(const std::string& name) const {
  return headers_.Get(name);
}

Request::Builder& Request::Builder::Body(const std::string& body) {
  body_ = body;
  has_body_ = true;
  // Default Content-Type: text/plain if not already set.
  if (!headers_.Has(HttpConstants::kLowerContentTypeHeader)) {
    headers_.Add(HttpConstants::kContentTypeHeader,
                 HttpConstants::kTextPlainMime);
  }
  return *this;
}

Request::Builder& Request::Builder::JsonBody(const std::string& json) {
  body_ = json;
  has_body_ = true;
  // Set or override Content-Type: application/json (replaces any existing
  // case-insensitive occurrences).
  headers_.Set(HttpConstants::kContentTypeHeader,
               HttpConstants::kApplicationJsonMime);
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
  for (const auto& [name, value] : headers_.fields()) {
    if (name.empty()) {
      return Result<Request>::Err(
          Error(ErrorCode::kInvalidArgument, "header name must not be empty"));
    }
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

  Request req(method_, url_, headers_.Build(), body_, has_body_, timeout_);
  return Result<Request>::Ok(std::move(req));
}

}  // namespace http
}  // namespace cpp_network
