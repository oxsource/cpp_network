#include "http/url.h"

#include <cctype>

namespace cpp_network {
namespace http {

namespace {

constexpr const char* kHttpScheme = "http";
constexpr const char* kHttpsScheme = "https";
constexpr std::uint16_t kDefaultHttpPort = 80;
constexpr std::uint16_t kDefaultHttpsPort = 443;

std::string ToLower(std::string s) {
  for (char& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

bool ContainsCrlf(const std::string& s) {
  return s.find('\r') != std::string::npos || s.find('\n') != std::string::npos;
}

bool IsHexDigit(char c) {
  return std::isdigit(static_cast<unsigned char>(c)) != 0 ||
         (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

int HexDigitValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return c - 'A' + 10;
}

bool IsUnreserved(unsigned char c) {
  return std::isalnum(c) != 0 || c == '-' || c == '.' || c == '_' || c == '~';
}

// Percent-encodes everything outside the unreserved set.
std::string PercentEncode(const std::string& value) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(value.size());
  for (unsigned char c : value) {
    if (IsUnreserved(c)) {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += kHex[c >> 4];
      out += kHex[c & 0x0F];
    }
  }
  return out;
}

// Decodes %XX escapes; when plus_as_space is set (query strings), '+' becomes
// a space. Returns false on truncated or non-hex escapes.
bool PercentDecode(const std::string& value, bool plus_as_space,
                   std::string* out) {
  out->clear();
  out->reserve(value.size());
  for (std::size_t i = 0; i < value.size(); ++i) {
    char c = value[i];
    if (c == '%' && i + 2 < value.size() && IsHexDigit(value[i + 1]) &&
        IsHexDigit(value[i + 2])) {
      out->push_back(static_cast<char>(
          (HexDigitValue(value[i + 1]) << 4) | HexDigitValue(value[i + 2])));
      i += 2;
    } else if (plus_as_space && c == '+') {
      out->push_back(' ');
    } else if (c == '%') {
      return false;
    } else {
      out->push_back(c);
    }
  }
  return true;
}

Result<Url> Invalid(const std::string& message) {
  return Result<Url>::Err(Error(ErrorCode::kInvalidArgument, message));
}

}  // namespace

std::uint16_t Url::port() const {
  if (explicit_port_.has_value()) {
    return *explicit_port_;
  }
  return scheme_ == kHttpsScheme ? kDefaultHttpsPort : kDefaultHttpPort;
}

Result<Url> Url::Parse(const std::string& url) {
  if (url.empty()) {
    return Invalid("URL must not be empty");
  }
  if (ContainsCrlf(url)) {
    return Invalid("URL must not contain CRLF");
  }

  // scheme "://" authority [path] ["?" query] ["#" fragment]
  const std::size_t scheme_end = url.find("://");
  if (scheme_end == std::string::npos) {
    return Invalid("URL must be absolute (scheme://...)");
  }
  std::string scheme = ToLower(url.substr(0, scheme_end));
  if (scheme != kHttpScheme && scheme != kHttpsScheme) {
    return Invalid("unsupported URL scheme: " + scheme);
  }

  std::string rest = url.substr(scheme_end + 3);
  if (rest.empty()) {
    return Invalid("URL is missing host");
  }

  // Fragment: everything after the first '#'.
  std::string fragment;
  const std::size_t hash = rest.find('#');
  if (hash != std::string::npos) {
    if (!PercentDecode(rest.substr(hash + 1), /*plus_as_space=*/false,
                       &fragment)) {
      return Invalid("invalid percent escape in fragment");
    }
    rest = rest.substr(0, hash);
  }

  // Query: everything after the first '?'.
  std::vector<Url::QueryParameter> params;
  const std::size_t question = rest.find('?');
  if (question != std::string::npos) {
    std::string query = rest.substr(question + 1);
    rest = rest.substr(0, question);
    std::size_t start = 0;
    while (start <= query.size()) {
      std::size_t amp = query.find('&', start);
      if (amp == std::string::npos) amp = query.size();
      std::string pair_str = query.substr(start, amp - start);
      if (!pair_str.empty()) {
        const std::size_t eq = pair_str.find('=');
        std::string name = pair_str.substr(0, eq == std::string::npos
                                                    ? pair_str.size()
                                                    : eq);
        std::string value =
            eq == std::string::npos ? "" : pair_str.substr(eq + 1);
        QueryParameter param;
        if (!PercentDecode(name, /*plus_as_space=*/true, &param.first) ||
            !PercentDecode(value, /*plus_as_space=*/true, &param.second)) {
          return Invalid("invalid percent escape in query");
        }
        params.push_back(std::move(param));
      }
      if (amp == query.size()) break;
      start = amp + 1;
    }
  }

  // Authority vs path.
  const std::size_t slash = rest.find('/');
  std::string authority = rest.substr(0, slash == std::string::npos
                                               ? rest.size()
                                               : slash);
  std::string path =
      slash == std::string::npos ? "" : rest.substr(slash);

  // Userinfo is not supported in v1.
  if (authority.find('@') != std::string::npos) {
    return Invalid("userinfo in URL is not supported");
  }

  // Host and optional port; IPv6 literals in brackets are handled.
  std::string host;
  std::optional<std::uint16_t> explicit_port;
  if (!authority.empty() && authority[0] == '[') {
    const std::size_t bracket_end = authority.find(']');
    if (bracket_end == std::string::npos) {
      return Invalid("unterminated IPv6 literal");
    }
    host = authority.substr(0, bracket_end + 1);
    if (bracket_end + 1 < authority.size()) {
      if (authority[bracket_end + 1] != ':') {
        return Invalid("invalid characters after IPv6 literal");
      }
      std::string port_str = authority.substr(bracket_end + 2);
      if (port_str.empty() ||
          port_str.find_first_not_of("0123456789") != std::string::npos) {
        return Invalid("invalid port");
      }
      unsigned long parsed = std::stoul(port_str);
      if (parsed < 1 || parsed > 65535) {
        return Invalid("port out of range");
      }
      explicit_port = static_cast<std::uint16_t>(parsed);
    }
  } else {
    const std::size_t colon = authority.rfind(':');
    if (colon != std::string::npos) {
      host = authority.substr(0, colon);
      std::string port_str = authority.substr(colon + 1);
      if (port_str.empty() ||
          port_str.find_first_not_of("0123456789") != std::string::npos) {
        return Invalid("invalid port");
      }
      unsigned long parsed = std::stoul(port_str);
      if (parsed < 1 || parsed > 65535) {
        return Invalid("port out of range");
      }
      explicit_port = static_cast<std::uint16_t>(parsed);
    } else {
      host = authority;
    }
  }
  host = ToLower(host);
  if (host.empty()) {
    return Invalid("URL is missing host");
  }
  if (host.find_first_of(" \t\"<>") != std::string::npos) {
    return Invalid("host contains invalid characters");
  }

  // Path segments (decoded); empty segments from consecutive slashes collapse.
  std::vector<std::string> segments;
  if (!path.empty()) {
    std::size_t start = 0;
    while (start < path.size()) {
      std::size_t next = path.find('/', start + 1);
      if (next == std::string::npos) next = path.size();
      std::string raw_segment = path.substr(
          start + 1, next - start - 1);
      if (!raw_segment.empty()) {
        std::string segment;
        if (!PercentDecode(raw_segment, /*plus_as_space=*/false, &segment)) {
          return Invalid("invalid percent escape in path");
        }
        segments.push_back(std::move(segment));
      }
      start = next;
    }
  }

  return Result<Url>::Ok(Url(std::move(scheme), std::move(host),
                             std::move(explicit_port), std::move(segments),
                             std::move(params), std::move(fragment)));
}

std::string Url::ToString() const {
  std::string out = scheme_ + "://" + host_;
  if (explicit_port_.has_value()) {
    out += ":" + std::to_string(*explicit_port_);
  }
  if (path_segments_.empty()) {
    out += "/";
  } else {
    for (const std::string& segment : path_segments_) {
      out += "/" + PercentEncode(segment);
    }
  }
  if (!query_parameters_.empty()) {
    out += "?";
    for (std::size_t i = 0; i < query_parameters_.size(); ++i) {
      if (i > 0) out += "&";
      out += PercentEncode(query_parameters_[i].first) + "=" +
              PercentEncode(query_parameters_[i].second);
    }
  }
  if (!fragment_.empty()) {
    out += "#" + PercentEncode(fragment_);
  }
  return out;
}

// static
Url::Builder Url::Builder::FromUrl(const Url& base) {
  return Builder(base);
}

Url::Builder::Builder(const Url& base)
    : scheme_(base.scheme_),
      host_(base.host_),
      port_(base.explicit_port_),
      has_port_(base.explicit_port_.has_value()),
      path_segments_(base.path_segments_),
      query_parameters_(base.query_parameters_),
      fragment_(base.fragment_) {}

Result<Url> Url::Builder::Build() const {
  std::string scheme = ToLower(scheme_);
  if (scheme != kHttpScheme && scheme != kHttpsScheme) {
    return Invalid("scheme must be http or https");
  }
  if (host_.empty()) {
    return Invalid("host must not be empty");
  }
  if (ContainsCrlf(host_) || host_.find_first_of(" \t\"<>") != std::string::npos) {
    return Invalid("host contains invalid characters");
  }
  if (has_port_ && port_ == 0) {
    return Invalid("port must be >= 1");
  }
  for (const std::string& segment : path_segments_) {
    if (segment.empty()) {
      return Invalid("path segments must not be empty");
    }
    if (ContainsCrlf(segment)) {
      return Invalid("path segments must not contain CRLF");
    }
  }
  for (const auto& [name, value] : query_parameters_) {
    if (name.empty()) {
      return Invalid("query parameter names must not be empty");
    }
    if (ContainsCrlf(name) || ContainsCrlf(value)) {
      return Invalid("query parameters must not contain CRLF");
    }
  }
  if (ContainsCrlf(fragment_)) {
    return Invalid("fragment must not contain CRLF");
  }

  return Result<Url>::Ok(Url(std::move(scheme), host_,
                             has_port_ ? port_
                                       : std::optional<std::uint16_t>(),
                             path_segments_, query_parameters_, fragment_));
}

}  // namespace http
}  // namespace cpp_network
