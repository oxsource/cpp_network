#include "http/response.h"

#include <cctype>
#include <utility>

namespace cpp_network {
namespace http {

struct Stream::Impl {
  // Streaming state for large-body responses (deferred to streaming-mode
  // enhancement). Currently unused: responses are buffered in memory.
};

Stream::Stream(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}
Stream::~Stream() = default;

Stream::Stream(Stream&&) noexcept = default;
Stream& Stream::operator=(Stream&&) noexcept = default;

std::int64_t Stream::Read(void* out, std::size_t max_bytes, Error* error) {
  (void)out;
  (void)max_bytes;
  if (error) *error = Error(ErrorCode::kInvalidState,
                            "streaming not supported in buffered mode");
  return -1;
}

std::optional<Stream> Response::stream() {
  if (!stream_impl_) return std::nullopt;
  return Stream(stream_impl_);
}

std::optional<std::string> Response::GetHeader(const std::string& name) const {
  return headers_.Get(name);
}

}  // namespace http
}  // namespace cpp_network
