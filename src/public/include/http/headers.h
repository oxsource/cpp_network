#ifndef CPP_NETWORK_HTTP_HEADERS_H_
#define CPP_NETWORK_HTTP_HEADERS_H_

#include <cctype>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "http/export.h"

namespace cpp_network {
namespace http {

// Ordered multimap of HTTP header fields (a value type, modeled after
// okhttp3.Headers).
//
// Preserves insertion order and duplicate field names ("Set-Cookie" style),
// while lookups are case-insensitive per RFC 9110. Immutable once built;
// compose instances through Headers::Builder.
class CPP_NETWORK_HTTP_EXPORT Headers {
 public:
  Headers() = default;

  // Case-insensitive first value for name, or nullopt if absent.
  std::optional<std::string> Get(const std::string& name) const {
    for (const auto& [key, value] : fields_) {
      if (EqualsIgnoreCase(key, name)) return value;
    }
    return std::nullopt;
  }

  // Case-insensitive list of all values for name, in field-line order.
  std::vector<std::string> GetAll(const std::string& name) const {
    std::vector<std::string> values;
    for (const auto& [key, value] : fields_) {
      if (EqualsIgnoreCase(key, name)) values.push_back(value);
    }
    return values;
  }

  bool Has(const std::string& name) const { return Get(name).has_value(); }

  int size() const { return static_cast<int>(fields_.size()); }
  bool empty() const { return fields_.empty(); }

  // Indexed access over field lines; duplicates occupy separate indexes.
  const std::string& name(int index) const { return fields_[index].first; }
  const std::string& value(int index) const { return fields_[index].second; }

  // Insertion-ordered (name, value) pairs including duplicates.
  const std::vector<std::pair<std::string, std::string>>& fields() const {
    return fields_;
  }

  // Field-line-wise equality: names compare case-insensitively (matching
  // lookup semantics), values and order must be exact.
  bool operator==(const Headers& other) const {
    if (fields_.size() != other.fields_.size()) return false;
    for (std::size_t i = 0; i < fields_.size(); ++i) {
      if (!EqualsIgnoreCase(fields_[i].first, other.fields_[i].first) ||
          fields_[i].second != other.fields_[i].second) {
        return false;
      }
    }
    return true;
  }
  bool operator!=(const Headers& other) const { return !(*this == other); }

  // Fluent builder producing an immutable Headers instance.
  class Builder {
   public:
    Builder() = default;

    // Appends a field line; duplicate names are allowed and preserved.
    Builder& Add(const std::string& name, const std::string& value) {
      fields_.emplace_back(name, value);
      return *this;
    }

    // Replaces every case-insensitive occurrence of name with a single field
    // line appended at the end (last-wins semantics, like okhttp's set()).
    Builder& Set(const std::string& name, const std::string& value) {
      Remove(name);
      fields_.emplace_back(name, value);
      return *this;
    }

    // Removes every case-insensitive occurrence of name; no-op if absent.
    Builder& Remove(const std::string& name) {
      for (auto it = fields_.begin(); it != fields_.end();) {
        if (EqualsIgnoreCase(it->first, name)) {
          it = fields_.erase(it);
        } else {
          ++it;
        }
      }
      return *this;
    }

    // Discards everything added so far.
    Builder& Clear() {
      fields_.clear();
      return *this;
    }

    // Case-insensitive first value accumulated so far, or nullopt.
    std::optional<std::string> Get(const std::string& name) const {
      for (const auto& [key, value] : fields_) {
        if (EqualsIgnoreCase(key, name)) return value;
      }
      return std::nullopt;
    }

    bool Has(const std::string& name) const { return Get(name).has_value(); }

    // Insertion-ordered pairs accumulated so far (including duplicates).
    const std::vector<std::pair<std::string, std::string>>& fields() const {
      return fields_;
    }

    Headers Build() const { return Headers(fields_); }

   private:
    friend class Headers;

    explicit Builder(std::vector<std::pair<std::string, std::string>> fields)
        : fields_(std::move(fields)) {}

    std::vector<std::pair<std::string, std::string>> fields_;
  };

 private:
  friend class Builder;

  explicit Headers(std::vector<std::pair<std::string, std::string>> fields)
      : fields_(std::move(fields)) {}

  static bool EqualsIgnoreCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
      if (std::tolower(static_cast<unsigned char>(a[i])) !=
          std::tolower(static_cast<unsigned char>(b[i]))) {
        return false;
      }
    }
    return true;
  }

  std::vector<std::pair<std::string, std::string>> fields_;
};

}  // namespace http
}  // namespace cpp_network

#endif  // CPP_NETWORK_HTTP_HEADERS_H_
