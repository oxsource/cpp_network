#include "http/http_umbrella.h"

#include <chrono>
#include <cstdio>
#include <string>

using cpp_network::http::Client;
using cpp_network::http::ErrorCode;
using cpp_network::http::Method;
using cpp_network::http::Options;
using cpp_network::http::Request;
using cpp_network::http::Response;
using cpp_network::http::Result;
using cpp_network::http::Tls;
using cpp_network::http::VerifyMode;

namespace {

// Platform trust anchor for verified-peer requests (FR-003 pattern): every
// demo section injects the OS store so verified behavior matches real apps.
std::string SystemCaBundle() {
#if defined(__APPLE__)
  return "/etc/ssl/cert.pem";
#else
  return "/etc/ssl/certs/ca-certificates.crt";
#endif
}

void PrintResponse(const Response& resp) {
  std::printf("  status: %d %s\n", resp.status(), resp.status_text().c_str());
  std::printf("  effective_url: %s\n", resp.effective_url().c_str());
  if (const auto& content_type = resp.GetHeader("Content-Type")) {
    std::printf("  content-type: %s\n", content_type->c_str());
  }
  std::string body = resp.body();
  const std::size_t kMaxSnippet = 200;
  if (body.size() > kMaxSnippet) {
    body.resize(kMaxSnippet);
    body += "...";
  }
  std::printf("  body (%zu bytes): %.200s\n", resp.body().size(),
              body.c_str());
}

void TestHttpsGetVerified() {
  std::printf("[1] HTTPS GET with default peer verification\n");
  Options options;
  options.SetTls(Tls::Builder().SetCaFile(SystemCaBundle()).Build());
  options.SetConnectTimeout(std::chrono::milliseconds(10000));
  options.SetTotalTimeout(std::chrono::milliseconds(15000));

  Result<Client> client = Client::Create(options);
  if (!client.ok()) {
    std::printf("  client create failed: %s\n",
                client.error().message().c_str());
    return;
  }

  Result<Response> response = client->Get("https://example.com");
  if (!response.ok()) {
    std::printf("  request failed: [%s] %s\n",
                ErrorCodeToString(response.error().code()),
                response.error().message().c_str());
    return;
  }
  PrintResponse(response.value());
}

void TestHttpsGetRequestBuilder() {
  std::printf("[2] HTTPS GET via Request::Builder (headers + timeout)\n");
  Result<Client> client = Client::Create(Options{}.SetTls(
      Tls::Builder().SetCaFile(SystemCaBundle()).Build()));
  if (!client.ok()) {
    std::printf("  client create failed: %s\n",
                client.error().message().c_str());
    return;
  }

  Result<Request> request =
      Request::Builder()
          .SetMethod(Method::kGet)
          .Url("https://httpbin.org/get")
          .Header("Accept", "application/json")
          .Header("User-Agent", "cpp-network-http-demo")
          .Timeout(std::chrono::milliseconds(10000))
          .Build();
  if (!request.ok()) {
    std::printf("  build failed: %s\n",
                request.error().message().c_str());
    return;
  }

  Result<Response> response = client->Send(request.value());
  if (!response.ok()) {
    std::printf("  request failed: [%s] %s\n",
                ErrorCodeToString(response.error().code()),
                response.error().message().c_str());
    return;
  }
  PrintResponse(response.value());
}

void TestHttpsPostJson() {
  std::printf("[3] HTTPS POST with JSON body\n");
  Result<Client> client = Client::Create(Options{}.SetTls(
      Tls::Builder().SetCaFile(SystemCaBundle()).Build()));
  if (!client.ok()) {
    std::printf("  client create failed: %s\n",
                client.error().message().c_str());
    return;
  }

  Result<Request> request = Request::Builder()
                                .SetMethod(Method::kPost)
                                .Url("https://httpbin.org/post")
                                .JsonBody("{\"hello\":\"https\"}")
                                .Timeout(std::chrono::milliseconds(10000))
                                .Build();
  if (!request.ok()) {
    std::printf("  build failed: %s\n",
                request.error().message().c_str());
    return;
  }

  Result<Response> response = client->Send(request.value());
  if (!response.ok()) {
    std::printf("  request failed: [%s] %s\n",
                ErrorCodeToString(response.error().code()),
                response.error().message().c_str());
    return;
  }
  PrintResponse(response.value());
}

void TestHttpsSkipVerification() {
  std::printf("[4] HTTPS GET with Tls skip verification\n");
  Tls tls =
      Tls::Builder().SetVerifyMode(VerifyMode::kSkipVerification).Build();
  Options options;
  options.SetTls(tls);

  Result<Client> client = Client::Create(options);
  if (!client.ok()) {
    std::printf("  client create failed: %s\n",
                client.error().message().c_str());
    return;
  }

  Result<Response> response = client->Head("https://example.com");
  if (!response.ok()) {
    std::printf("  request failed: [%s] %s\n",
                ErrorCodeToString(response.error().code()),
                response.error().message().c_str());
    return;
  }
  std::printf("  status: %d %s\n", response->status(),
              response->status_text().c_str());
}

}  // namespace

int main() {
  std::puts("cpp_network https demo");

  TestHttpsGetVerified();
  TestHttpsGetRequestBuilder();
  TestHttpsPostJson();
  TestHttpsSkipVerification();

  return 0;
}
