#ifndef CPP_NETWORK_HTTP_RESULT_H_
#define CPP_NETWORK_HTTP_RESULT_H_

#include <optional>
#include <utility>

#include "http/error.h"
#include "http/export.h"

namespace cpp_network {
namespace http {

// Value-or-error result for synchronous API calls. Errors never throw;
// failures are carried as a non-ok() Error.
template <typename T>
class CPP_NETWORK_HTTP_EXPORT Result {
 public:
  static Result<T> Ok(T value) {
    Result<T> r;
    r.ok_ = true;
    r.value_.emplace(std::move(value));
    return r;
  }
  static Result<T> Err(Error error) {
    Result<T> r;
    r.ok_ = false;
    r.error_ = std::move(error);
    return r;
  }

  bool ok() const { return ok_; }
  const T& value() const { return *value_; }
  T& value() { return *value_; }
  const Error& error() const { return error_; }

  T& operator*() { return *value_; }
  const T& operator*() const { return *value_; }
  T* operator->() { return &*value_; }
  const T* operator->() const { return &*value_; }

  T TakeValue() { return std::move(*value_); }

 private:
  Result() : ok_(false) {}

  bool ok_;
  std::optional<T> value_;
  Error error_;
};

// Specialization for void-typed results (e.g. validation).
template <>
class CPP_NETWORK_HTTP_EXPORT Result<void> {
 public:
  Result() = delete;

  static Result<void> Ok() { return Result<void>(Error()); }
  static Result<void> Ok(Error) { return Result<void>(Error()); }
  static Result<void> Err(Error error) { return Result<void>(std::move(error)); }

  bool ok() const { return error_.ok(); }
  const Error& error() const { return error_; }

 private:
  explicit Result(Error error) : error_(std::move(error)) {}

  Error error_;
};

}  // namespace http
}  // namespace cpp_network

#endif  // CPP_NETWORK_HTTP_RESULT_H_
