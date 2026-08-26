# Quickstart: C++ Cross-Platform Network Library

**Branch**: `001-cpp-network-library` | **Date**: 2026-08-26 | **Spec**: [spec.md](spec.md)

## Overview

This library provides a cross-platform C++ HTTP client with a promise-based, axios-inspired API. It supports HTTP/1.1 (v1) with WebSocket planned (v2), and adapts TLS per platform: OpenSSL on host (macOS/Linux), BoringSSL on Android.

## Getting Started

### Prerequisites

- Bazel 6.5
- C++17 compiler (Clang/GCC)
- OpenSSL (host), BoringSSL (Android) — fetched via Bazel

### Add the library to your Bazel project

Reference the workspace and add the public target as a dependency:

```python
deps = ["//:netlib"]  # or the shared library target
```

### Platform setup (host)

Follow the graph_runtime convention: run the platform setup script once to generate `.user.bazelrc`.

## Usage

### 1. Implement an Executor

The library does not manage threads or run an event loop. Provide an executor with task submission, delayed scheduling, and **fd-watching** (backed by your thread pool + event loop):

```cpp
#include "netlib/executor.h"

class MyExecutor : public netlib::Executor {
 public:
  void Submit(std::function<void()> task) override {
    pool_.Post(std::move(task));
  }
  void Schedule(std::chrono::milliseconds delay,
                std::function<void()> task) override {
    pool_.PostDelayed(delay, std::move(task));
  }
  void WatchFd(int fd, uint32_t events,
               std::function<void(uint32_t)> callback) override {
    loop_.WatchFd(fd, events, std::move(callback));   // epoll/kqueue/libuv/asio
  }
  void UnwatchFd(int fd) override {
    loop_.UnwatchFd(fd);
  }
 private:
  // Your thread pool + event loop.
};

MyExecutor g_executor;
```

### 2. Create an HttpClient

```cpp
#include "netlib/netlib.h"

netlib::HttpClient client = netlib::HttpClient::Config()
    .SetExecutor(&g_executor)
    .SetConnectTimeout(std::chrono::seconds(5))
    .SetReadTimeout(std::chrono::seconds(10))
    .SetFollowRedirects(true)
    .Build();
```

### 3. Send a GET request (axios style)

```cpp
client.Get("https://httpbin.org/get")
    .Then([](netlib::HttpResponse resp) {
      printf("Status: %d\n", resp.status_code());
      printf("Body: %s\n", resp.body_string().c_str());
    })
    .Catch([](const netlib::Error& err) {
      fprintf(stderr, "Error: %s\n", err.message().c_str());
    });
```

### 4. Send a POST request with JSON body

```cpp
client.Post("https://httpbin.org/post", R"({"name":"netlib"})")
    .Then([](netlib::HttpResponse resp) {
      // handle response
    })
    .Catch([](const netlib::Error& err) { /* handle error */ });
```

### 5. Custom headers and per-request config

```cpp
netlib::HttpRequest req = netlib::HttpRequest::Builder()
    .Method(netlib::HttpMethod::kPost)
    .Url("https://api.example.com/upload")
    .Header("Authorization", "Bearer token")
    .Header("Content-Type", "application/json")
    .JsonBody(R"({"key":"value"})")
    .Build();

client.Send(req)
    .Then([](netlib::HttpResponse resp) { /* ... */ })
    .Catch([](const netlib::Error& err) { /* ... */ });
```

## Next Steps

- TLS configuration (CA certs, skipping verification) → see [data-model.md](data-model.md) and public API contract.
- Streaming large response bodies → use `HttpResponse::body_stream()`.
- WebSocket support is planned for a future phase.
