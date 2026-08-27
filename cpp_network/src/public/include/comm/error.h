#ifndef CPP_NETWORK_COMM_ERROR_H_
#define CPP_NETWORK_COMM_ERROR_H_

#include <string>

#include "comm/export.h"

namespace cpp_network {
namespace comm {

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

  // Close details carried by websocket-layer failures (specs/006 data-model
  // CloseInfo): populated when a connection ends because the PEER sent a
  // close frame. Unused by HTTP errors; code 0 means "unknown/absent".
  void set_close_code(uint16_t value) { close_code_ = value; }
  void set_close_reason(std::string reason) { close_reason_ = std::move(reason); }
  uint16_t close_code() const { return close_code_; }
  const std::string& close_reason() const { return close_reason_; }

 private:
  ErrorCode code_;
  std::string message_;
  uint16_t close_code_ = 0;
  std::string close_reason_;
};

}  // namespace comm
}  // namespace cpp_network

#endif  // CPP_NETWORK_COMM_ERROR_H_
