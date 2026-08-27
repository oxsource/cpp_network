#ifndef CPP_NETWORK_HTTP_RESPONSE_H_
#define CPP_NETWORK_HTTP_RESPONSE_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "comm/export.h"
#include "http/request.h"

namespace cpp_network {
namespace http {

// Synchronous readable body stream for large responses (streaming mode).
class CPP_NETWORK_HTTP_EXPORT Stream {
 public:
  Stream() = delete;
  ~Stream();

  Stream(Stream&&) noexcept;
  Stream& operator=(Stream&&) noexcept;
  Stream(const Stream&) = delete;
  Stream& operator=(const Stream&) = delete;

  // Reads up to max_bytes into out; returns bytes read (0 = EOF) or negative on
  // error.
  std::int64_t Read(void* out, std::size_t max_bytes, comm::Error* error);

 private:
  friend class Response;
  struct Impl;
  explicit Stream(std::shared_ptr<Impl> impl);
  std::shared_ptr<Impl> impl_;
};

// Immutable representation of an incoming HTTP response.
class CPP_NETWORK_HTTP_EXPORT Response {
 public:
  Response() = delete;

  int status() const { return status_; }
  const std::string& status_text() const { return status_text_; }
  const Headers& headers() const { return headers_; }
  bool has_body() const { return has_body_; }
  const std::string& body() const { return body_; }
  bool ok() const { return status_ >= 200 && status_ < 300; }
  const std::string& effective_url() const { return effective_url_; }
  std::optional<std::string> GetHeader(const std::string& name) const;

  // Returns a stream handle if the response body is in streaming mode
  // (large body), otherwise nullopt.
  std::optional<Stream> stream();

 private:
  friend class Client;
  friend class Engine;
  friend class Stream;

  Response(int status, std::string status_text, Headers headers,
           std::string body, bool has_body, std::string effective_url,
           std::shared_ptr<Stream::Impl> stream_impl)
      : status_(status),
        status_text_(std::move(status_text)),
        headers_(std::move(headers)),
        body_(std::move(body)),
        has_body_(has_body),
        effective_url_(std::move(effective_url)),
        stream_impl_(std::move(stream_impl)) {}

  int status_;
  std::string status_text_;
  Headers headers_;
  std::string body_;
  bool has_body_;
  std::string effective_url_;
  std::shared_ptr<Stream::Impl> stream_impl_;
};

}  // namespace http
}  // namespace cpp_network

#endif  // CPP_NETWORK_HTTP_RESPONSE_H_
