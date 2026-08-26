#ifndef CPP_NETWORK_HTTP_OPTIONS_H_
#define CPP_NETWORK_HTTP_OPTIONS_H_

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

#include "http/export.h"
#include "http/result.h"
#include "http/tls.h"

namespace cpp_network {
namespace http {

struct Proxy {
  std::string host;
  uint16_t port = 8080;
};

// Configuration for a Client: timeouts, redirects, interface binding, proxy,
// connection pool, and TLS.
class CPP_NETWORK_HTTP_EXPORT Options {
 public:
  Options() = default;

  std::chrono::milliseconds connect_timeout() const { return connect_timeout_; }
  std::chrono::milliseconds read_timeout() const { return read_timeout_; }
  std::chrono::milliseconds write_timeout() const { return write_timeout_; }
  std::chrono::milliseconds total_timeout() const { return total_timeout_; }
  bool follow_redirects() const { return follow_redirects_; }
  int max_redirects() const { return max_redirects_; }
  const std::optional<std::string>& interface() const { return interface_; }
  const std::optional<std::string>& local_address() const {
    return local_address_;
  }
  const std::optional<int>& local_port() const { return local_port_; }
  const std::optional<Proxy>& proxy() const { return proxy_; }
  int max_connections_per_host() const { return max_connections_per_host_; }
  std::chrono::milliseconds keep_alive() const { return keep_alive_; }
  const Tls& tls() const { return tls_; }

  Options& SetConnectTimeout(std::chrono::milliseconds ms) {
    connect_timeout_ = ms;
    return *this;
  }
  Options& SetReadTimeout(std::chrono::milliseconds ms) {
    read_timeout_ = ms;
    return *this;
  }
  Options& SetWriteTimeout(std::chrono::milliseconds ms) {
    write_timeout_ = ms;
    return *this;
  }
  Options& SetTotalTimeout(std::chrono::milliseconds ms) {
    total_timeout_ = ms;
    return *this;
  }
  Options& SetFollowRedirects(bool follow) {
    follow_redirects_ = follow;
    return *this;
  }
  Options& SetMaxRedirects(int n) {
    max_redirects_ = n;
    return *this;
  }
  Options& SetInterface(const std::string& name) {
    interface_ = name;
    return *this;
  }
  Options& SetLocalAddress(const std::string& ip) {
    local_address_ = ip;
    return *this;
  }
  Options& SetLocalPort(int port) {
    local_port_ = port;
    return *this;
  }
  Options& SetProxy(const std::string& host, uint16_t port) {
    proxy_ = Proxy{host, port};
    return *this;
  }
  Options& SetMaxConnectionsPerHost(int n) {
    max_connections_per_host_ = n;
    return *this;
  }
  Options& SetKeepAlive(std::chrono::milliseconds ms) {
    keep_alive_ = ms;
    return *this;
  }
  Options& SetTls(const Tls& tls) {
    tls_ = tls;
    return *this;
  }

  Result<void> Validate() const;

 private:
  std::chrono::milliseconds connect_timeout_{10000};
  std::chrono::milliseconds read_timeout_{30000};
  std::chrono::milliseconds write_timeout_{30000};
  std::chrono::milliseconds total_timeout_{0};
  bool follow_redirects_ = true;
  int max_redirects_ = 20;
  std::optional<std::string> interface_;
  std::optional<std::string> local_address_;
  std::optional<int> local_port_;
  std::optional<Proxy> proxy_;
  int max_connections_per_host_ = 5;
  std::chrono::milliseconds keep_alive_{120000};
  Tls tls_;
};

}  // namespace http
}  // namespace cpp_network

#endif  // CPP_NETWORK_HTTP_OPTIONS_H_
