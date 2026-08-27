#ifndef CPP_NETWORK_COMM_URL_H_
#define CPP_NETWORK_COMM_URL_H_

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "comm/export.h"
#include "comm/result.h"

namespace cpp_network {
namespace comm {

// Parsed/composable absolute HTTP(S) URL, inspired by okhttp's HttpUrl.
// Parse() decomposes a URL into scheme/host/port/path segments/query
// parameters/fragment (percent-decoded); Builder composes and percent-encodes
// them back. ToString() always yields a wire-safe URL.
class CPP_NETWORK_HTTP_EXPORT Url {
 public:
  // Query parameter as (decoded name, decoded value).
  using QueryParameter = std::pair<std::string, std::string>;

  // Parses an absolute http:// or https:// URL. Rejects CRLF, userinfo,
  // malformed ports, invalid percent escapes, and non-http(s) schemes with
  // Error(kInvalidArgument).
  static Result<Url> Parse(const std::string& url);

  const std::string& scheme() const { return scheme_; }
  const std::string& host() const { return host_; }
  // Explicit port from the authority; nullopt when absent.
  const std::optional<std::uint16_t>& explicit_port() const {
    return explicit_port_;
  }
  // Effective port: explicit if present, otherwise the scheme default
  // (http=80, https=443).
  std::uint16_t port() const;
  // Decoded path segments; empty for a bare authority URL ("/" is implied).
  const std::vector<std::string>& path_segments() const {
    return path_segments_;
  }
  // Decoded query parameters in order of appearance.
  const std::vector<QueryParameter>& query_parameters() const {
    return query_parameters_;
  }
  const std::string& fragment() const { return fragment_; }

  // Re-serializes to an encoded URL string (default port is omitted).
  std::string ToString() const;

  class CPP_NETWORK_HTTP_EXPORT Builder {
   public:
    Builder() = default;

    // Pre-populates the builder from an existing URL.
    static Builder FromUrl(const Url& base);

    Builder& SetScheme(const std::string& scheme) {
      scheme_ = scheme;
      return *this;
    }
    Builder& SetHost(const std::string& host) {
      host_ = host;
      return *this;
    }
    Builder& SetPort(std::uint16_t port) {
      port_ = port;
      has_port_ = true;
      return *this;
    }
    Builder& ClearPort() {
      has_port_ = false;
      return *this;
    }
    // Appends one path segment; the value is percent-encoded on Build
    // ('/' inside a segment does not create additional levels).
    Builder& AddPathSegment(const std::string& segment) {
      path_segments_.push_back(segment);
      return *this;
    }
    // Appends ?name=value (both percent-encoded on Build).
    Builder& AddQueryParameter(const std::string& name,
                               const std::string& value) {
      query_parameters_.emplace_back(name, value);
      return *this;
    }
    Builder& SetFragment(const std::string& fragment) {
      fragment_ = fragment;
      return *this;
    }

    Result<Url> Build() const;

   private:
    explicit Builder(const Url& base);

    std::string scheme_;
    std::string host_;
    std::optional<std::uint16_t> port_;
    bool has_port_ = false;
    std::vector<std::string> path_segments_;
    std::vector<QueryParameter> query_parameters_;
    std::string fragment_;
  };

 private:
  Url(std::string scheme, std::string host,
      std::optional<std::uint16_t> explicit_port,
      std::vector<std::string> path_segments,
      std::vector<QueryParameter> query_parameters, std::string fragment)
      : scheme_(std::move(scheme)),
        host_(std::move(host)),
        explicit_port_(explicit_port),
        path_segments_(std::move(path_segments)),
        query_parameters_(std::move(query_parameters)),
        fragment_(std::move(fragment)) {}

  std::string scheme_;
  std::string host_;
  std::optional<std::uint16_t> explicit_port_;
  std::vector<std::string> path_segments_;
  std::vector<QueryParameter> query_parameters_;
  std::string fragment_;
};

}  // namespace comm
}  // namespace cpp_network

#endif  // CPP_NETWORK_COMM_URL_H_
