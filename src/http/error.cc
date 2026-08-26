#include "http/error.h"

namespace cpp_network {
namespace http {

const char* ErrorCodeToString(ErrorCode code) {
  switch (code) {
    case ErrorCode::kNone:
      return "kNone";
    case ErrorCode::kInvalidArgument:
      return "kInvalidArgument";
    case ErrorCode::kInvalidState:
      return "kInvalidState";
    case ErrorCode::kProtocolError:
      return "kProtocolError";
    case ErrorCode::kMalformedResponse:
      return "kMalformedResponse";
    case ErrorCode::kUnsupportedProtocol:
      return "kUnsupportedProtocol";
    case ErrorCode::kDnsResolutionFailed:
      return "kDnsResolutionFailed";
    case ErrorCode::kConnectionRefused:
      return "kConnectionRefused";
    case ErrorCode::kConnectionClosed:
      return "kConnectionClosed";
    case ErrorCode::kConnectionTimeout:
      return "kConnectionTimeout";
    case ErrorCode::kReadTimeout:
      return "kReadTimeout";
    case ErrorCode::kWriteTimeout:
      return "kWriteTimeout";
    case ErrorCode::kTotalTimeout:
      return "kTotalTimeout";
    case ErrorCode::kTlsHandshakeFailed:
      return "kTlsHandshakeFailed";
    case ErrorCode::kCertificateVerificationFailed:
      return "kCertificateVerificationFailed";
    case ErrorCode::kTooManyRedirects:
      return "kTooManyRedirects";
    case ErrorCode::kOutOfMemory:
      return "kOutOfMemory";
    case ErrorCode::kCancelled:
      return "kCancelled";
    case ErrorCode::kInternalError:
      return "kInternalError";
  }
  return "kUnknown";
}

}  // namespace http
}  // namespace cpp_network
