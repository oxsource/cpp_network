# 公共 API 契约：HTTP 同步实现

**Branch**: `003-http-implementation` | **Date**: 2026-08-26 | **Spec**: [spec.md](../spec.md)

公共 API 位于 `cpp_network::http` 命名空间（两层），经 `src/public/include/http/` 头暴露。同步阻塞，`Result<T>` 错误返回，不暴露 libcurl 类型。

## 命名规范

- **命名空间**: `cpp_network::http`（根 `cpp_network` + 协议 `http`；未来 `cpp_network::ws`）。
- **类名**: `Client` / `Request` / `Response` / `Options` / `Tls` / `Method` / `Error` / `Result`。
- **文件名**: 无协议前缀（`client.h` 等）；协议由 include 目录层 `include/http/` 区分，避免跨协议头冲突。
- **include 目录**: `include/http/`（按协议分目录，不以 netlib 命名）。

## 头文件布局

```text
src/public/include/http/
├── http_umbrella.h   # umbrella: includes all headers below
├── client.h          # Client
├── request.h         # Request / Method
├── response.h        # Response / Stream
├── options.h         # Options / Proxy
├── tls.h             # Tls / VerifyMode
├── error.h           # Error / ErrorCode
├── result.h          # Result<T>
└── export.h          # CPP_NETWORK_HTTP_API export macro
```

**注**: 公共导出宏命名 `CPP_NETWORK_HTTP_API`（对齐 `cpp_network::http` 命名空间）。

## 核心类型

所有类型在 `namespace cpp_network::http { ... }` 内定义（下文省略命名空间包裹）。

### Error / ErrorCode

### Error / ErrorCode

```cpp
enum class ErrorCode {
  kNone, kInvalidArgument, kInvalidState, kProtocolError, kMalformedResponse,
  kUnsupportedProtocol, kDnsResolutionFailed, kConnectionRefused,
  kConnectionClosed, kConnectionTimeout, kReadTimeout, kWriteTimeout,
  kTotalTimeout, kTlsHandshakeFailed, kCertificateVerificationFailed,
  kTooManyRedirects, kOutOfMemory, kCancelled, kInternalError,
};
class Error {
 public:
  Error();                       // kNone
  Error(ErrorCode code, std::string message);
  ErrorCode code() const;
  const std::string& message() const;
  bool ok() const;
};
```

### Result<T>

```cpp
template <typename T>
class Result {
 public:
  bool ok() const;
  const T& value() const;   // valid when ok()==true
  T& value();
  const Error& error() const;  // valid when ok()==false
  T TakeValue();
  static Result<T> Ok(T value);
  static Result<T> Err(Error error);
};
```

### Method

```cpp
enum class Method { kGet, kPost, kPut, kDelete, kPatch, kHead, kOptions };
```

### Headers

```cpp
// Order-preserving multi-map of header fields that allows duplicates
// (modeled on okhttp3.Headers); immutable value type.
// Lookup (Get/GetAll/Has) is case-insensitive.
class Headers {
 public:
  std::optional<std::string> Get(const std::string& name) const;   // first match
  std::vector<std::string> GetAll(const std::string& name) const;  // all values, in order
  bool Has(const std::string& name) const;
  int size() const;                                    // number of field lines (incl. duplicates)
  const std::string& name(int index) const;
  const std::string& value(int index) const;
  const std::vector<std::pair<std::string, std::string>>& fields() const;

  bool operator==(const Headers&) const;   // name case-insensitive; value/order exact
  bool operator!=(const Headers&) const;

  class Builder {
   public:
    Builder& Add(const std::string& name, const std::string& value);   // appends
    Builder& Set(const std::string& name, const std::string& value);   // replaces all with same name
    Builder& Remove(const std::string& name);
    Builder& Clear();
    Headers Build() const;
  };
};
```

### Request

```cpp
class Request {
 public:
  Method method() const;
  const std::string& url() const;
  const Headers& headers() const;        // order-preserving multi-map w/ duplicates; case-insensitive lookup
  bool has_body() const;
  const std::string& body() const;
  const std::optional<std::chrono::milliseconds>& timeout() const;
  std::optional<std::string> GetHeader(const std::string& name) const;

  class Builder {
   public:
    Builder& Method(Method m);
    Builder& Url(const std::string& url);
    Builder& Header(const std::string& name, const std::string& value);
    Builder& Body(const std::string& body);        // defaults to Content-Type: text/plain
    Builder& JsonBody(const std::string& json);    // Content-Type: application/json
    Builder& Timeout(std::chrono::milliseconds ms);
    Result<Request> Build() const;                 // invalid → kInvalidArgument
  };
};
```

### Response

```cpp
class Response {
 public:
  int status() const;
  const std::string& status_text() const;
  const Headers& headers() const;
  bool has_body() const;
  const std::string& body() const;             // buffered mode
  std::optional<Stream> stream();              // streaming for large bodies (>8MB)
  bool ok() const;                             // 2xx
  const std::string& effective_url() const;
};
```

### Tls

```cpp
enum class VerifyMode { kVerifyPeer, kSkipVerification };

// Immutable configuration object; cannot be modified once built via Tls::Builder.
class Tls {
 public:
  // Read-only accessors: verify_mode() / ca_pem() / ca_file() /
  //                      client_cert() / client_key() / sni()

  class Builder {
   public:
    // CA certificates: in-memory PEM or file path (mutually exclusive; Validate() rejects both)
    Builder& SetCaPem(const std::string& pem);
    Builder& SetCaFile(const std::string& path);
    // Client certificate (mTLS): PEM or file path
    Builder& SetCertificate(const std::string& cert,
                            const std::string& key);
    // SNI / verify mode
    Builder& SetSni(const std::string& hostname);
    Builder& SetVerifyMode(VerifyMode mode);

    Tls Build() const;
  };
};
```

### Options

```cpp
class Options {
 public:
  Options& SetConnectTimeout(std::chrono::milliseconds);
  Options& SetReadTimeout(std::chrono::milliseconds);
  Options& SetWriteTimeout(std::chrono::milliseconds);
  Options& SetTotalTimeout(std::chrono::milliseconds);
  Options& SetFollowRedirects(bool);
  Options& SetMaxRedirects(int);
  // Network interface / source address / source port
  Options& SetInterface(const std::string& name);       // "eth1" / IP / "if!eth1" / "host!ip"
  Options& SetLocalAddress(const std::string& ip);      // source IP
  Options& SetLocalPort(int port);                      // source port (optional)
  Options& SetProxy(const std::string& host, uint16_t port);
  Options& SetMaxConnectionsPerHost(int);
  Options& SetKeepAlive(std::chrono::milliseconds);
  Options& SetTls(const Tls& tls);
  Result<void> Validate() const;
};
```

### Client

```cpp
class Client {
 public:
  // Creates the client; returns an error if Options validation fails.
  static Result<Client> Create(const Options& options);

  Result<Response> Get(const std::string& url);
  Result<Response> Get(const Request& req);
  Result<Response> Post(const std::string& url, const std::string& body);
  Result<Response> Post(const Request& req);
  Result<Response> Put(const std::string& url, const std::string& body);
  Result<Response> Delete(const std::string& url);
  Result<Response> Patch(const std::string& url, const std::string& body);
  Result<Response> Head(const std::string& url);
  Result<Response> Options(const std::string& url);
  Result<Response> Send(const Request& req);   // general entry point

  void Close();
  ~Client();
  // Not copyable
};
```

## 契约不变量

1. **同步阻塞**：`Send`/`Get`/`Post` 阻塞调用线程直至完成；库内无线程/事件循环。
2. **错误经 Result**：所有失败以非 ok() 的 `Result` 返回，不抛异常、不崩溃。
3. **不可变请求/响应**：`Request`/`Response` 构造后不可变，并发只读安全。
4. **流式**：大 body（>8MB）经 `stream()` 同步块读。
5. **网卡绑定**：`SetInterface`/`SetLocalAddress` 映射 `CURLOPT_INTERFACE`；`SetLocalPort` 映射 `CURLOPT_LOCALPORT`。
6. **证书配置**：CA（`CURLOPT_CAINFO[_BLOB]`）、mTLS（`CURLOPT_SSLCERT/SSLKEY`）、SNI、skip 校验。
7. **超时语义**：total 约束单次传输；connect/read/write 细化各阶段。
8. **重定向默认**：`follow_redirects=true`、`max_redirects=20`。
9. **TLS 默认**：`kVerifyPeer`（安全默认）。
10. **线程模型**：`Client` 多线程并发 `Send` 经内部 mutex 串行化进共享 CURLM。
11. **平台无关**：公共 API 无 curl/SSL 类型（FR-013）。

## 验收命令（实现阶段 gate）

```bash
./tools/platform_setup.sh
bazel build //...                         # zero errors/warnings
bazel test //...                          # all pass: smoke + http_integration + https + config
# Local integration test coverage:
#   US1: 200/404/POST-JSON
#   US2: remote HTTPS + self-signed cert (fails by default / inject CA / skip) + mTLS
#   US3: connect timeout / redirects / interface binding
```
