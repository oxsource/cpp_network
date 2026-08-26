#include "http/client.h"

#include <memory>
#include <string>

#include "engine.h"
#include "http/error.h"

namespace cpp_network {
namespace http {

class Client::Impl {
 public:
  explicit Impl(const Options& options) : engine(options) {}
  Engine engine;
};

Result<Client> Client::Create(const Options& options) {
  Result<void> validation = options.Validate();
  if (!validation.ok()) {
    return Result<Client>::Err(validation.error());
  }
  auto impl = std::make_unique<Impl>(options);
  return Result<Client>::Ok(Client(std::move(impl)));
}

Client::Client(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

Client::~Client() = default;
Client::Client(Client&&) noexcept = default;
Client& Client::operator=(Client&&) noexcept = default;

Result<Response> Client::Get(const std::string& url) {
  return Get(Request::Builder().SetMethod(Method::kGet).Url(url).Build().TakeValue());
}

Result<Response> Client::Get(const Request& req) { return Send(req); }

Result<Response> Client::Post(const std::string& url, const std::string& body) {
  return Post(Request::Builder().SetMethod(Method::kPost).Url(url).Body(body).Build().TakeValue());
}

Result<Response> Client::Post(const Request& req) { return Send(req); }

Result<Response> Client::Put(const std::string& url, const std::string& body) {
  return Send(Request::Builder().SetMethod(Method::kPut).Url(url).Body(body).Build().TakeValue());
}

Result<Response> Client::Delete(const std::string& url) {
  return Send(Request::Builder().SetMethod(Method::kDelete).Url(url).Build().TakeValue());
}

Result<Response> Client::Patch(const std::string& url, const std::string& body) {
  return Send(Request::Builder().SetMethod(Method::kPatch).Url(url).Body(body).Build().TakeValue());
}

Result<Response> Client::Head(const std::string& url) {
  return Send(Request::Builder().SetMethod(Method::kHead).Url(url).Build().TakeValue());
}

Result<Response> Client::SendOptions(const std::string& url) {
  return Send(Request::Builder().SetMethod(Method::kOptions).Url(url).Build().TakeValue());
}

Result<Response> Client::Send(const Request& req) { return impl_->engine.Send(req); }

void Client::Close() { impl_->engine.Close(); }

}  // namespace http
}  // namespace cpp_network
