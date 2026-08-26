#ifndef CPP_NETWORK_HTTP_ERROR_H_
#define CPP_NETWORK_HTTP_ERROR_H_

#include <string>

#include "http/export.h"

namespace cpp_network {
namespace http {

enum class ErrorCode {
  kNone = 0,
  kInvalidArgument,
  kInvalidState,
  kProtocolError,
  kMalformedResponse,
  kUnsupportedProtocol,
  kDnsResolutionFailed,
  kConnectionRefused,
  kConnectionClosed,
  kConnectionTimeout,
  kReadTimeout,
  kWriteTimeout,
  kTotalTimeout,
  kTlsHandshakeFailed,
  kCertificateVerificationFailed,
  kTooManyRedirects,
  kOutOfMemory,
  kCancelled,
  kInternalError,
};

CPP_NETWORK_HTTP_EXPORT const char* ErrorCodeToString(ErrorCode code);

class CPP_NETWORK_HTTP_EXPORT Error {
 public:
  Error() : code_(ErrorCode::kNone) {}
  Error(ErrorCode code, std::string message)
      : code_(code), message_(std::move(message)) {}

  ErrorCode code() const { return code_; }
  const std::string& message() const { return message_; }
  bool ok() const { return code_ == ErrorCode::kNone; }

 private:
  ErrorCode code_;
  std::string message_;
};

}  // namespace http
}  // namespace cpp_network

#endif  // CPP_NETWORK_HTTP_ERROR_H_
