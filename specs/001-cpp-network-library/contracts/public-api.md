# Public API Contract: C++ Cross-Platform Network Library

**Branch**: `001-cpp-network-library` | **Date**: 2026-08-26 | **Spec**: [spec.md](../spec.md)

This document defines the public API contract for the library. All types live under the `netlib` namespace and are exposed via the `public/include/netlib/` headers. The API is platform-independent (no platform ifdefs in user code) and follows Google C++ Style Guide.

## Namespace

```
netlib
```

## Core Types

### `Error`

Value type carrying an error code and message.

```cpp
namespace netlib {
enum class ErrorCode {
  kNone,
  kInvalidArgument,
  kDnsResolutionFailed,
  kConnectionRefused,
  kConnectionTimeout,
  kReadTimeout,
  kWriteTimeout,
  kTlsHandshakeFailed,
  kCertificateVerificationFailed,
  kProtocolError,
  kMalformedResponse,
  kConnectionPoolExhausted,
  kRedirectLimitExceeded,
  kCancelled,
};

class Error {
 public:
  ErrorCode code() const;
  const std::string& message() const;
  bool ok() const;
};
}  // namespace netlib
```

### `Promise<T>`

Promise/future-like composable async value. Execution is delegated to the user-provided executor.

```cpp
namespace netlib {

template <typename T>
class Promise {
 public:
  // Compose success continuation (runs on executor).
  template <typename F>
  auto Then(F&& fn) -> Promise<invoke_result_t<F, T>>;
  // Compose error continuation.
  Promise<T> Catch(std::function<void(const Error&)> fn);
  // Register a completion callback (success or error).
  Promise<T> Finally(std::function<void()> fn);

  // For compatibility: block on completion (uses an external blocking primitive,
  // not the library's own threads).
  T Wait();       // throws on error
  T Get();        // alias for Wait
  bool IsReady() const;
};

}  // namespace netlib
```

## HTTP Types

### `HttpMethod`

```cpp
enum class HttpMethod {
  kGet, kPost, kPut, kDelete, kPatch, kHead, kOptions,
};
```

### `HttpRequest`

Immutable request. Created via `HttpRequest::Builder`.

```cpp
class HttpRequest {
 public:
  HttpMethod method() const;
  const Url& url() const;
  const Headers& headers() const;
  const std::optional<Body>& body() const;

  class Builder {
   public:
    Builder& Method(HttpMethod m);
    Builder& Url(const std::string& url);
    Builder& Header(const std::string& name, const std::string& value);
    Builder& Headers(const Headers& headers);
    Builder& Body(const std::string& body);          // sets Content-Type text/plain if absent
    Builder& JsonBody(const std::string& json);      // sets Content-Type application/json
    Builder& Timeout(std::chrono::milliseconds ms);  // per-request override
    HttpRequest Build() const;                       // validates; throws/errors on invalid
  };
};
```

### `HttpResponse`

Immutable response.

```cpp
class HttpResponse {
 public:
  int status_code() const;
  const std::string& status_text() const;
  const Headers& headers() const;
  const std::string& body_string() const;   // buffered body
  // Streaming access for large bodies (FR-007):
  std::optional<BodyStream> body_stream();
};
```

### `HttpClient`

Main entry point. Constructed via `HttpClient::Config`.

```cpp
class HttpClient {
 public:
  // Configuration (fluent builder).
  class Config {
   public:
    Config& SetExecutor(Executor* executor);              // REQUIRED
    Config& SetConnectTimeout(std::chrono::milliseconds);
    Config& SetReadTimeout(std::chrono::milliseconds);
    Config& SetWriteTimeout(std::chrono::milliseconds);
    Config& SetTotalTimeout(std::chrono::milliseconds);
    Config& SetRetryPolicy(RetryPolicy policy);
    Config& SetProxy(const std::string& proxy_host, uint16_t port);
    Config& SetFollowRedirects(bool follow);
    Config& SetMaxRedirects(int n);
    Config& SetTlsConfig(TlsConfig config);
    Config& SetMaxConnectionsPerHost(int n);
    Config& SetKeepAlive(std::chrono::milliseconds);
    HttpClient Build() const;  // REQUIRED: executor must be set
  };

  // axios-inspired request methods (all return Promise<HttpResponse>).
  Promise<HttpResponse> Get(const std::string& url);
  Promise<HttpResponse> Get(const HttpRequest& req);
  Promise<HttpResponse> Post(const std::string& url, const std::string& body);
  Promise<HttpResponse> Post(const HttpRequest& req);
  Promise<HttpResponse> Put(const std::string& url, const std::string& body);
  Promise<HttpResponse> Delete(const std::string& url);
  Promise<HttpResponse> Patch(const std::string& url, const std::string& body);
  Promise<HttpResponse> Head(const std::string& url);
  Promise<HttpResponse> Options(const std::string& url);
  Promise<HttpResponse> Send(const HttpRequest& req);  // generic

  void Close();  // close pooled connections
};
```

## TLS Types

### `TlsConfig`

```cpp
enum class VerifyMode { kVerifyPeer, kSkipVerification };

class TlsConfig {
 public:
  VerifyMode verify_mode() const;
  // Custom CA certificates (PEM/DER). Empty => system trust store.
  const std::vector<std::string>& ca_certificates() const;
  const std::optional<std::string>& client_certificate() const;
  const std::optional<std::string>& sni_hostname() const;
};
```

## Executor (external)

### `Executor`

User-provided abstraction. The library calls these; it never creates threads. Provides task submission, delayed scheduling, **and fd-watching** (needed to drive libcurl's multi interface from an external event loop).

```cpp
class Executor {
 public:
  // Schedule a task for execution.
  virtual void Submit(std::function<void()> task) = 0;
  // Schedule a task after a delay (used for timeouts, retry backoff, curl timers).
  virtual void Schedule(std::chrono::milliseconds delay,
                        std::function<void()> task) = 0;
  // Watch a file descriptor for the given events (POLLIN|POLLOUT|POLLERR).
  // callback is invoked with the ready event mask. UnwatchFd cancels.
  virtual void WatchFd(int fd, uint32_t events,
                       std::function<void(uint32_t)> callback) = 0;
  virtual void UnwatchFd(int fd) = 0;
  virtual ~Executor() = default;
};
```

**Note**: The user implements `WatchFd`/`UnwatchFd` on top of their event loop (epoll/kqueue/libuv/asio). This is how libcurl's `CURLMOPT_SOCKETFUNCTION`/`CURLMOPT_TIMERFUNCTION` callbacks are bridged to the external scheduler.

## Contract Invariants

1. **Platform independence**: All public headers compile unchanged on macOS, Linux, and Android. No `#ifdef` in user code (FR-016). TLS backend (OpenSSL/BoringSSL) is selected at libcurl build time, invisible to the API.
2. **External scheduling**: The library never spawns threads and never runs an internal event loop; all async work and fd-watching is dispatched through `Executor` (FR-018, FR-021).
3. **Error propagation**: All failures surface as rejected `Promise` values carrying a non-`ok()` `Error`; never through undefined behavior or crashes (FR-013).
4. **Immutable requests/responses**: `HttpRequest` and `HttpResponse` are immutable after construction; concurrency-safe to share (FR-011).
5. **Streaming**: Large bodies are accessible via `body_stream()` to bound memory (FR-007, SC-007).
6. **Timeout semantics**: `total_timeout` bounds the entire request lifecycle; connect/read/write timeouts bound individual phases (mapped to libcurl `CURLOPT_*TIMEOUT*`).
7. **Redirect default**: `follow_redirects = true`, `max_redirects = 20` by default (libcurl `CURLOPT_FOLLOWLOCATION`/`CURLOPT_MAXREDIRS`).
8. **Retry default**: no retries by default; opt-in via `RetryPolicy`.
9. **TLS verification default**: `kVerifyPeer` (secure by default; maps to `CURLOPT_SSL_VERIFYPEER`/`CURLOPT_SSL_VERIFYHOST`).
10. **Event bridge**: libcurl socket/timer events are bridged to the executor via `WatchFd`/`Schedule`; the executor must remain alive for the lifetime of the `HttpClient`.

## ABI/Style Notes

- Headers use include guards matching file names (`NETLIB_HTTP_CLIENT_H_`).
- Public symbols export via `NETLIB_API` macro (mirrors graph_runtime `GRAPH_RUNTIME_API`), only on the shared-library build.
- C++17 required; `std::optional`, `std::function`, `std::chrono` used.
- libcurl is a private implementation dependency; its types never appear in the public API (only in `src/http/` internal code). This keeps FR-016 intact and allows swapping engines in the future.
