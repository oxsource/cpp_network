# 公共 API 契约：HTTP 同步实现

**Branch**: `003-http-implementation` | **Date**: 2026-08-26 | **Spec**: [spec.md](../spec.md)

公共 API 位于 `netlib` 命名空间，经 `src/public/include/netlib/` 头暴露。同步阻塞，`Result<T>` 错误返回，不暴露 libcurl 类型。

## 类命名（简化优雅）

`Client` / `Request` / `Response` / `Options` / `Tls` / `Method` / `Error` / `Result`。

## 头文件布局

```text
src/public/include/netlib/
├── netlib.h            # umbrella：include 下列全部
├── client.h            # Client
├── request.h           # Request / Method
├── response.h          # Response / Stream
├── options.h           # Options / Proxy
├── tls.h               # Tls / VerifyMode
├── error.h             # Error / ErrorCode
├── result.h            # Result<T>
└── netlib_export.h     # NETLIB_API
```

## 核心类型

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
  const T& value() const;   // ok()==true 有效
  T& value();
  const Error& error() const;  // ok()==false 有效
  T TakeValue();
  static Result<T> Ok(T value);
  static Result<T> Err(Error error);
};
```

### Method

```cpp
enum class Method { kGet, kPost, kPut, kDelete, kPatch, kHead, kOptions };
```

### Request

```cpp
class Request {
 public:
  Method method() const;
  const std::string& url() const;
  const Headers& headers() const;        // vector<pair<string,string>>，保序
  bool has_body() const;
  const std::string& body() const;
  const std::optional<std::chrono::milliseconds>& timeout() const;
  std::optional<std::string> GetHeader(const std::string& name) const;

  class Builder {
   public:
    Builder& Method(Method m);
    Builder& Url(const std::string& url);
    Builder& Header(const std::string& name, const std::string& value);
    Builder& Body(const std::string& body);        // 默认 Content-Type: text/plain
    Builder& JsonBody(const std::string& json);    // Content-Type: application/json
    Builder& Timeout(std::chrono::milliseconds ms);
    Result<Request> Build() const;                 // 非法 → kInvalidArgument
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
  const std::string& body() const;             // 缓冲模式
  std::optional<Stream> stream();              // 大 body 流式（>8MB）
  bool ok() const;                             // 2xx
  const std::string& effective_url() const;
};
```

### Tls

```cpp
enum class VerifyMode { kVerifyPeer, kSkipVerification };

class Tls {
 public:
  // CA 证书：内存 PEM 或文件路径
  Tls& SetCaCertificate(const std::string& pem);
  Tls& SetCaFile(const std::string& path);
  // 客户端证书（mTLS）
  Tls& SetClientCertificate(const std::string& cert, const std::string& key);
  // SNI / 校验模式
  Tls& SetSni(const std::string& hostname);
  Tls& SetVerifyMode(VerifyMode mode);
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
  // 指定网卡 / 源地址 / 源端口
  Options& SetInterface(const std::string& name);       // "eth1" / IP / "if!eth1" / "host!ip"
  Options& SetLocalAddress(const std::string& ip);      // 源 IP
  Options& SetLocalPort(int port);                      // 源端口（可选）
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
  // 构建；Options 校验失败返回错误。
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
  Result<Response> Send(const Request& req);   // 通用入口

  void Close();
  ~Client();
  // 不可拷贝
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
bazel build //...                         # 零 error/warning
bazel test //...                          # smoke + http_integration + https + config 全部通过
# 本地集成测试覆盖：
#   US1: 200/404/POST-JSON
#   US2: HTTPS 远程 + 自签证书(默认失败/注入CA/skip) + mTLS
#   US3: 连接超时/重定向/指定网卡
```
