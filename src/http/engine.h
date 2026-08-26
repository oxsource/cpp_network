#ifndef CPP_NETWORK_HTTP_ENGINE_H_
#define CPP_NETWORK_HTTP_ENGINE_H_

#include <curl/curl.h>

#include <mutex>
#include <memory>
#include <string>

#include "http/options.h"
#include "http/request.h"
#include "http/response.h"
#include "http/result.h"

namespace cpp_network {
namespace http {

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
