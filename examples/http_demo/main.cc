#include "http/http_umbrella.h"

#include <cstdio>
#include <string>

namespace {

using cpp_network::http::Client;
using cpp_network::http::Options;
using cpp_network::http::Request;

void PrintResponse(const std::string& label,
                   const cpp_network::http::Response& resp) {
  std::printf("== %s ==\n", label.c_str());
  std::printf("status: %d %s\n", resp.status(), resp.status_text().c_str());
  std::printf("effective_url: %s\n", resp.effective_url().c_str());
  const std::string& body = resp.body();
  const std::size_t kMaxBodyPrint = 512;
  if (body.size() > kMaxBodyPrint) {
    std::printf("body (%zu bytes, truncated):\n%.512s...\n", body.size(),
                body.c_str());
  } else {
    std::printf("body (%zu bytes):\n%s\n", body.size(), body.c_str());
  }
  std::printf("\n");
}

}  // namespace

int main() {
  Options options;
  options.SetConnectTimeout(std::chrono::milliseconds(5000))
      .SetReadTimeout(std::chrono::milliseconds(10000))
      .SetFollowRedirects(true);

  auto client_result = Client::Create(options);
  if (!client_result.ok()) {
    const cpp_network::http::Error& err = client_result.error();
    std::printf("client create failed: %s (%s)\n",
                cpp_network::http::ErrorCodeToString(err.code()),
                err.message().c_str());
    return 1;
  }
  Client client = client_result.TakeValue();

  // GET example.
  auto get_result = client.Get("https://httpbin.org/get");
  if (get_result.ok()) {
    PrintResponse("GET https://httpbin.org/get", get_result.value());
  } else {
    const cpp_network::http::Error& err = get_result.error();
    std::printf("GET failed: %s (%s)\n\n",
                cpp_network::http::ErrorCodeToString(err.code()),
                err.message().c_str());
  }

  // POST with a JSON body via Request::Builder.
  auto post_request =
      Request::Builder()
          .SetMethod(cpp_network::http::Method::kPost)
          .Url("https://httpbin.org/post")
          .JsonBody(R"({"name":"netlib","demo":true})")
          .Header("X-Demo-Client", "cpp_network-http_demo")
          .Timeout(std::chrono::milliseconds(10000))
          .Build();
  if (!post_request.ok()) {
    std::printf("request build failed: %s\n",
                post_request.error().message().c_str());
    return 1;
  }

  auto post_result = client.Send(post_request.value());
  if (post_result.ok()) {
    PrintResponse("POST https://httpbin.org/post", post_result.value());
  } else {
    const cpp_network::http::Error& err = post_result.error();
    std::printf("POST failed: %s (%s)\n",
                cpp_network::http::ErrorCodeToString(err.code()),
                err.message().c_str());
    return 1;
  }

  client.Close();
  return 0;
}
