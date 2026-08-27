#ifndef CPP_NETWORK_HTTP_ENGINE_H_
#define CPP_NETWORK_HTTP_ENGINE_H_

#include <curl/curl.h>

#include <mutex>
#include <memory>
#include <string>

#include "comm/options.h"
#include "http/request.h"
#include "http/response.h"
#include "comm/result.h"


#include "comm/error.h"
#include "comm/tls.h"
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




// Internal synchronous transfer engine: a shared CURLM guarded by a mutex.
// Send blocks the calling thread via curl_multi_poll until the transfer
// completes, reusing pooled connections across requests.
class Engine {
 public:
  explicit Engine(const Options& options);
  ~Engine();

  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  Result<Response> Send(const Request& req);
  void Close();

 private:
  struct TransferContext;

  Result<Response> PerformSingle(const Request& req);

  std::mutex mu_;
  CURLM* multi_;
  Options options_;
  bool closed_;
};

}  // namespace http
}  // namespace cpp_network

#endif  // CPP_NETWORK_HTTP_ENGINE_H_
