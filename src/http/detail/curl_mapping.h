#ifndef CPP_NETWORK_HTTP_CURL_MAPPING_H_
#define CPP_NETWORK_HTTP_CURL_MAPPING_H_

#include <chrono>

#include <curl/curl.h>

#include "http/error.h"
#include "http/options.h"
#include "http/request.h"

namespace cpp_network {
namespace http {
namespace detail {

// Resolves the effective hard transfer timeout (CURLOPT_TIMEOUT_MS):
// request-level override > client total_timeout > write_timeout (fallback cap;
// libcurl has no dedicated write-phase timeout). Returns kNone when no hard
// timeout applies; otherwise *out carries the resolved duration and the return
// value is the ErrorCode to report on CURLE_OPERATION_TIMEDOUT.
__attribute__((visibility("hidden")))
ErrorCode EffectiveHardTimeout(const Request& req, const Options& options,
                               std::chrono::milliseconds* out);

// Applies Request + Options to a curl easy handle. `headers_out` receives a
// curl_slist that MUST be kept alive until the transfer completes (libcurl does
// not copy the header list), and freed via curl_slist_free_all by the caller.
// Returns a non-ok() Error on mapping/validation failure.
__attribute__((visibility("hidden")))
Error ApplyEasyOptions(CURL* easy, const Request& req, const Options& options,
                       curl_slist** headers_out);

// Applies multi-level options (connection pool) to a CURLM handle.
__attribute__((visibility("hidden")))
void ApplyMultiOptions(CURLM* multi, const Options& options);

}  // namespace detail
}  // namespace http
}  // namespace cpp_network

#endif  // CPP_NETWORK_HTTP_CURL_MAPPING_H_
