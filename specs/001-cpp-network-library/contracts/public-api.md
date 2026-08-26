# Public API Contract: C++ Cross-Platform Network Library

**Branch**: `001-cpp-network-library` | **Date**: 2026-08-26（同步 API 重构版） | **Spec**: [spec.md](../spec.md)

本文档定义库的公共 API 契约。所有类型位于 `netlib` 命名空间，经 `public/include/netlib/` 头文件暴露。API 平台无关（用户代码无平台 ifdef），遵循 Google C++ Style Guide，C++17。

**核心模型**：**同步阻塞 API**。`Send` 阻塞调用线程直到请求完成，直接返回 `HttpResponse`。库内无 Promise/协程/事件循环/线程；异步与流程编排由上层实现（上层用线程池/协程/事件循环调用本库）。

## Namespace

```
netlib
```

## Core Types

### `Error`

```cpp
namespace netlib {
enum class ErrorCode {
  kNone = 0,
  kInvalidArgument,            // URL 非法、配置非法、必填缺失
  kInvalidState,               // 非法状态调用（如 client 已关闭）
  kProtocolError,              // 一般协议错误
  kMalformedResponse,          // 响应格式非法
  kUnsupportedProtocol,        // 不支持的 scheme
  kDnsResolutionFailed,        // DNS 解析失败
  kConnectionRefused,          // 连接被拒绝
  kConnectionClosed,           // 连接被对端关闭
  kConnectionTimeout,          // 连接阶段超时
  kReadTimeout,                // 读超时
  kWriteTimeout,               // 写超时
  kTotalTimeout,               // 总超时
  kTlsHandshakeFailed,         // TLS 握手失败
  kCertificateVerificationFailed,  // 证书校验失败
  kTooManyRedirects,           // 重定向次数超限
  kOutOfMemory,                // 内存不足
  kCancelled,                  // 操作被取消
  kInternalError,              // 引擎内部错误
};

class Error {
 public:
  Error();
  Error(ErrorCode code, std::string message);
  ErrorCode code() const;
  const std::string& message() const;
  bool ok() const;
};
}  // namespace netlib
```

**错误策略（同步）**：库以返回值暴露结果。`Send` 类接口返回 `Result<HttpResponse>`（见下），成功含响应，失败含 `Error`。**不抛异常**（保持 C++17 兼容与可预测错误流）；配置构建失败也可用 `Result` 返回。

### `Result<T>`（同步结果）

```cpp
namespace netlib {
template <typename T>
class Result {
 public:
  bool ok() const;              // 是否成功
  const T& value() const;       // 成功值（ok() 为 true 时有效）
  T& value();
  const Error& error() const;   // 失败错误（ok() 为 false 时有效）
  T TakeValue();                // 移动取出值
  // 便捷：static 构造
  static Result<T> Ok(T value);
  static Result<T> Err(Error error);
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

不可变请求，`HttpRequest::Builder` 构造。

```cpp
class HttpRequest {
 public:
  HttpMethod method() const;
  const std::string& url() const;
  const Headers& headers() const;
  bool has_body() const;
  const std::string& body() const;
  const std::optional<std::chrono::milliseconds>& timeout() const;

  class Builder {
   public:
    Builder& Method(HttpMethod m);
    Builder& Url(const std::string& url);
    Builder& Header(const std::string& name, const std::string& value);
    Builder& Body(const std::string& body);       // Content-Type: text/plain（若未设）
    Builder& JsonBody(const std::string& json);   // Content-Type: application/json
    Builder& Timeout(std::chrono::milliseconds ms);
    Result<HttpRequest> Build() const;            // 校验失败返回 kInvalidArgument
  };
};
```

### `HttpResponse`

不可变响应，同步持有 body。

```cpp
class HttpResponse {
 public:
  int status_code() const;
  const std::string& status_text() const;
  const Headers& headers() const;
  bool has_body() const;
  const std::string& body_string() const;     // 完整缓冲 body
  // 大 body 流式（同步阻塞读取，SC-007）：仅在响应过大时可用
  std::optional<BodyStream> body_stream();
  bool ok() const;                            // 2xx
  const std::string& effective_url() const;   // 重定向后最终 URL
};
```

### `HttpClient`（同步）

```cpp
class HttpClient {
 public:
  class Config {
   public:
    Config& SetConnectTimeout(std::chrono::milliseconds);
    Config& SetReadTimeout(std::chrono::milliseconds);
    Config& SetWriteTimeout(std::chrono::milliseconds);
    Config& SetTotalTimeout(std::chrono::milliseconds);
    Config& SetRetryPolicy(const RetryPolicy& policy);   // 见下
    Config& SetProxy(const std::string& host, uint16_t port);
    Config& SetFollowRedirects(bool follow);
    Config& SetMaxRedirects(int n);
    Config& SetTlsConfig(const TlsConfig& config);
    Config& SetMaxConnectionsPerHost(int n);
    Config& SetKeepAlive(std::chrono::milliseconds);
    Result<HttpClient> Build() const;   // 校验失败返回错误
  };

  // —— 同步 axios 风格方法（全部阻塞调用线程，直接返回 Result<HttpResponse>）——
  Result<HttpResponse> Get(const std::string& url);
  Result<HttpResponse> Get(const HttpRequest& req);
  Result<HttpResponse> Post(const std::string& url, const std::string& body);
  Result<HttpResponse> Put(const std::string& url, const std::string& body);
  Result<HttpResponse> Delete(const std::string& url);
  Result<HttpResponse> Patch(const std::string& url, const std::string& body);
  Result<HttpResponse> Head(const std::string& url);
  Result<HttpResponse> Options(const std::string& url);
  Result<HttpResponse> Send(const HttpRequest& req);   // 通用入口

  void Close();   // 关闭连接池（线程安全，可在其他线程调用）

  // 并发语义：可在任意多个线程同时调用 Send（内部经共享 CURLM 串行化，连接池复用）
};
```

## TLS Types

### `TlsConfig`

```cpp
enum class VerifyMode { kVerifyPeer, kSkipVerification };

class TlsConfig {
 public:
  VerifyMode verify_mode() const;                    // 默认 kVerifyPeer
  const std::vector<std::string>& ca_certificates() const;
  const std::optional<std::string>& client_certificate() const;
  const std::optional<std::string>& client_private_key() const;
  const std::optional<std::string>& sni_hostname() const;

  class Builder {
   public:
    Builder& SetVerifyMode(VerifyMode mode);
    Builder& AddCaCertificate(const std::string& pem);
    Builder& SetClientCertificate(const std::string& pem, const std::string& key);
    Builder& SetSniHostname(const std::string& hostname);
    Result<TlsConfig> Build() const;
  };
};
```

## 配置类型（NetworkConfig / RetryPolicy / Proxy）

```cpp
struct RetryPolicy {
  // 注意：库内单次传输；重试由上层循环调用 Send 实现。
  // 该类型保留用于文档与校验（v1 库不自动重试）。
  int max_retries = 0;
  std::chrono::milliseconds retry_delay{100};
  enum class Condition { kNone, kNetworkError, kNetworkErrorOr5xx };
  Condition condition = Condition::kNone;
};

struct NetworkConfig {
  std::chrono::milliseconds connect_timeout{10000};
  std::chrono::milliseconds read_timeout{30000};
  std::chrono::milliseconds write_timeout{30000};
  std::chrono::milliseconds total_timeout{0};
  bool follow_redirects = true;
  int max_redirects = 20;
  RetryPolicy retry_policy;
  std::optional<Proxy> proxy;
  TlsConfig tls_config;
  int max_connections_per_host = 5;
  std::chrono::milliseconds keep_alive{120000};
  Result<void> Validate() const;
};
```

## Contract Invariants

1. **平台无关**：所有公共头在 macOS/Linux/Android 编译一致，无用户代码 ifdef（FR-016）。TLS 后端构建时选择，API 不可见。
2. **同步阻塞**：`Send`/`Get`/`Post` 阻塞调用线程直至完成；库不创建线程、不回调、不提供异步原语（FR-018 修订为同步）。
3. **错误经 `Result`**：所有失败以非 `ok()` 的 `Result` 返回；不抛异常、不崩溃（FR-013）。
4. **不可变请求/响应**：`HttpRequest`/`HttpResponse` 构造后不可变，可并发只读共享（FR-011）。
5. **流式**：大 body 经 `body_stream()` 同步块读（SC-007）。
6. **超时语义**：`total_timeout` 限制单次传输；connect/read/write 细化各阶段（映射见 http-config-mapping.md）。
7. **重定向默认**：`follow_redirects=true`、`max_redirects=20`。
8. **重试默认**：库内不重试（上层自行循环）；`RetryPolicy` 仅作配置承载。
9. **TLS 校验默认**：`kVerifyPeer`（安全默认）。
10. **线程模型**：`HttpClient` 线程安全（多线程并发 `Send` 经内部 mutex 串行化进共享 CURLM）；`Close()` 线程安全。

## ABI/Style Notes

- 头文件 include guard 与文件名一致（`NETLIB_HTTP_CLIENT_H_`）。
- 公共符号经 `NETLIB_API` 宏导出（shared 构建，镜像 graph_runtime `GRAPH_RUNTIME_API`）。
- C++17；`std::optional`/`std::chrono`/`std::string`。
- libcurl 是私有实现依赖；其类型（`CURL*`/`CURLM*`/`curl_slist`）绝不进入公共 API（FR-016）。
