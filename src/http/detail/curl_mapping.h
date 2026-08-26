#ifndef CPP_NETWORK_HTTP_CURL_MAPPING_H_
#define CPP_NETWORK_HTTP_CURL_MAPPING_H_

#include <curl/curl.h>

#include "http/options.h"
#include "http/request.h"

namespace cpp_network {
namespace http {
namespace detail {

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
