#ifndef CPP_NETWORK_HTTP_DETAIL_HTTP_CONSTANTS_H_
#define CPP_NETWORK_HTTP_DETAIL_HTTP_CONSTANTS_H_

namespace cpp_network {
namespace http {
namespace detail {

// Single source of truth for HTTP wire-format string literals used inside the
// http implementation (URL schemes, well-known headers, MIME types, method
// tokens). Not part of the public API.
class HttpConstants {
 public:
  // Absolute-URL schemes accepted by Request validation.
  static constexpr char kHttpScheme[] = "http://";
  static constexpr char kHttpsScheme[] = "https://";

  // Well-known header name (canonical spelling used when emitting).
  static constexpr char kContentTypeHeader[] = "Content-Type";
  // Lowercase form for case-insensitive comparisons.
  static constexpr char kLowerContentTypeHeader[] = "content-type";

  // MIME types applied by Request::Builder.
  static constexpr char kTextPlainMime[] = "text/plain";
  static constexpr char kApplicationJsonMime[] = "application/json";

  // Method tokens (HTTP/1.1).
  static constexpr char kMethodGet[] = "GET";
  static constexpr char kMethodPost[] = "POST";
  static constexpr char kMethodPut[] = "PUT";
  static constexpr char kMethodDelete[] = "DELETE";
  static constexpr char kMethodPatch[] = "PATCH";
  static constexpr char kMethodHead[] = "HEAD";
  static constexpr char kMethodOptions[] = "OPTIONS";

 private:
  HttpConstants() = delete;
};

}  // namespace detail
}  // namespace http
}  // namespace cpp_network

#endif  // CPP_NETWORK_HTTP_DETAIL_HTTP_CONSTANTS_H_
