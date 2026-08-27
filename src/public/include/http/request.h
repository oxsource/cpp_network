#ifndef CPP_NETWORK_HTTP_REQUEST_H_
#define CPP_NETWORK_HTTP_REQUEST_H_

#include <chrono>
#include <optional>
#include <string>
#include <utility>

#include "http/headers.h"
#include "http/method.h"
#include "http/export.h"
#include "http/result.h"
#include "http/url.h"

namespace cpp_network {
namespace http {

// Immutable description of an outgoing HTTP request, built via Request::Builder.
class CPP_NETWORK_HTTP_EXPORT Request {
 public:
  Request() = delete;

  Method method() const { return method_; }
  const std::string& url() const { return url_; }
  const Headers& headers() const { return headers_; }
  bool has_body() const { return has_body_; }
  const std::string& body() const { return body_; }
  const std::optional<std::chrono::milliseconds>& timeout() const {
    return timeout_;
  }

  std::optional<std::string> GetHeader(const std::string& name) const;

  class CPP_NETWORK_HTTP_EXPORT Builder {
   public:
    Builder() = default;

    Builder& SetMethod(Method method) {
      method_ = method;
      return *this;
    }
    Builder& Url(const std::string& url) {
      url_ = url;
      return *this;
    }
    // Composes the request URL from a Url object (encoded via ToString()).
    // The return type is spelled out because the method name hides the class.
    Builder& Url(const ::cpp_network::http::Url& url) {
      url_ = url.ToString();
      return *this;
    }
    Builder& Header(const std::string& name, const std::string& value) {
      headers_.Add(name, value);
      return *this;
    }
    Builder& SetHeaders(const Headers& headers) {
      headers_ = Headers::Builder();
      for (const auto& [name, value] : headers.fields()) {
        headers_.Add(name, value);
      }
      return *this;
    }
    Builder& Body(const std::string& body);
    Builder& JsonBody(const std::string& json);
    Builder& Timeout(std::chrono::milliseconds ms) {
      timeout_ = ms;
      return *this;
    }

    Result<Request> Build() const;

   private:
    Method method_ = Method::kGet;
    std::string url_;
    Headers::Builder headers_;
    std::string body_;
    bool has_body_ = false;
    std::optional<std::chrono::milliseconds> timeout_;
  };

 private:
  Request(Method method, std::string url, Headers headers, std::string body,
          bool has_body, std::optional<std::chrono::milliseconds> timeout)
      : method_(method),
        url_(std::move(url)),
        headers_(std::move(headers)),
        body_(std::move(body)),
        has_body_(has_body),
        timeout_(timeout) {}

  Method method_;
  std::string url_;
  Headers headers_;
  std::string body_;
  bool has_body_;
  std::optional<std::chrono::milliseconds> timeout_;

  friend class Builder;
};

}  // namespace http
}  // namespace cpp_network

#endif  // CPP_NETWORK_HTTP_REQUEST_H_
