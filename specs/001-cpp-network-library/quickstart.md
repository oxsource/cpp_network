# Quickstart: C++ Cross-Platform Network Library

**Branch**: `001-cpp-network-library` | **Date**: 2026-08-26（同步重构版） | **Spec**: [spec.md](spec.md)

## Overview

跨平台 C++ 网络库，**同步阻塞 API**，axios 风格易用。基于 libcurl，支持 HTTP/1.1 (v1) + WebSocket (v2)。TLS 统一使用 OpenSSL（全平台 host + Android）。库内无线程/事件循环/回调；异步由上层用线程池/协程包装。

## Getting Started

### Prerequisites

- Bazel 6.5
- C++17 编译器（Clang/GCC）
- libcurl / OpenSSL（经 Bazel 拉取）

### Add the library to your Bazel project

```python
deps = ["//:netlib"]  # or the shared library target
```

### 平台设置（host）

按 graph_runtime 惯例运行平台设置脚本生成 `.user.bazelrc`。

## Usage

### 1. 创建 HttpClient

```cpp
#include "netlib/netlib.h"

netlib::HttpClient client = netlib::HttpClient::Config()
    .SetConnectTimeout(std::chrono::seconds(5))
    .SetReadTimeout(std::chrono::seconds(10))
    .SetFollowRedirects(true)
    .Build()
    .value();   // on failure an error is returned in the Result
```

### 2. 发送 GET 请求（阻塞，直接返回结果）

```cpp
netlib::Result<netlib::HttpResponse> res = client.Get("https://httpbin.org/get");
if (!res.ok()) {
  fprintf(stderr, "Error: %s\n", res.error().message().c_str());
  return;
}
printf("Status: %d\n", res->status_code());
printf("Body: %s\n", res->body_string().c_str());
```

### 3. 发送 POST 请求（JSON body）

```cpp
netlib::HttpRequest req = netlib::HttpRequest::Builder()
    .Method(netlib::HttpMethod::kPost)
    .Url("https://httpbin.org/post")
    .Header("Authorization", "Bearer token")
    .JsonBody(R"({"name":"netlib"})")
    .Build()
    .value();

auto res = client.Send(req);
if (!res.ok()) { /* handle error */ }
```

### 4. 自定义 headers / per-request timeout

```cpp
netlib::HttpRequest req = netlib::HttpRequest::Builder()
    .Method(netlib::HttpMethod::kGet)
    .Url("https://api.example.com/upload")
    .Header("X-Custom", "value")
    .Timeout(std::chrono::seconds(3))   // overrides the client-level value
    .Build().value();
```

### 5. 上层异步化（示例：线程池包装）

库本身同步阻塞；上层用线程池/协程实现并发：

```cpp
std::vector<std::future<netlib::Result<netlib::HttpResponse>>> futures;
for (auto& url : urls) {
  futures.push_back(std::async(std::launch::async, [&client, url] {
    return client.Get(url);
  }));
}
for (auto& f : futures) {
  auto res = f.get();
  // ...
}
```

## Next Steps

- TLS 配置（CA certs、跳过校验）→ 见 data-model.md 与 public API contract。
- 流式大 body → `HttpResponse::body_stream()` 同步块读。
- 重试 → 上层循环调用 `Send`（库内单次传输）。
- WebSocket 支持为未来阶段。
