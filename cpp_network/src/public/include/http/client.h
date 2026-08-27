#ifndef CPP_NETWORK_HTTP_CLIENT_H_
#define CPP_NETWORK_HTTP_CLIENT_H_

#include <memory>
#include <string>

#include "comm/export.h"
#include "comm/options.h"
#include "http/request.h"
#include "http/response.h"
#include "comm/result.h"

namespace cpp_network {
namespace http {

// Synchronous HTTP client. Blocks the calling thread until a request completes.
// Thread-safe for concurrent Send calls (serialized via an internal shared
// CURLM + mutex).
class CPP_NETWORK_HTTP_EXPORT Client {
 public:
  Client() = delete;

  static comm::Result<Client> Create(const comm::Options& options);

  comm::Result<Response> Get(const std::string& url);
  comm::Result<Response> Get(const Request& req);
  comm::Result<Response> Post(const std::string& url, const std::string& body);
  comm::Result<Response> Post(const Request& req);
  comm::Result<Response> Put(const std::string& url, const std::string& body);
  comm::Result<Response> Delete(const std::string& url);
  comm::Result<Response> Patch(const std::string& url, const std::string& body);
  comm::Result<Response> Head(const std::string& url);
  comm::Result<Response> SendOptions(const std::string& url);
  comm::Result<Response> Send(const Request& req);

  void Close();

  ~Client();

  Client(Client&&) noexcept;
  Client& operator=(Client&&) noexcept;
  Client(const Client&) = delete;
  Client& operator=(const Client&) = delete;

 private:
  class Impl;
  explicit Client(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

}  // namespace http
}  // namespace cpp_network

#endif  // CPP_NETWORK_HTTP_CLIENT_H_
