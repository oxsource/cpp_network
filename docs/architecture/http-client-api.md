# Client 同步 API（已实现）

**Branch**: `003-http-implementation` | **Date**: 2026-08-26

**对应需求**: FR-001（axios 风格同步 API）、FR-005（Result 错误策略）

**实现位置**: `src/public/include/http/client.h`、`src/http/client.cc`

## Overview

`cpp_network::http::Client` 是面向用户的 HTTP 客户端：静态工厂 `Create(const Options&)` 构造，快捷方法（Get/Post/...）与通用 `Send(Request)` 全部同步阻塞，失败以 `Result<Response>` 返回。

## 实际 API

```cpp
class Client {
 public:
  static Result<Client> Create(const Options& options);   // Options::Validate 前置校验

  Result<Response> Get(const std::string& url);
  Result<Response> Get(const Request& req);
  Result<Response> Post(const std::string& url, const std::string& body);
  Result<Response> Post(const Request& req);
  Result<Response> Put(const std::string& url, const std::string& body);
  Result<Response> Delete(const std::string& url);
  Result<Response> Patch(const std::string& url, const std::string& body);
  Result<Response> Head(const std::string& url);
  Result<Response> SendOptions(const std::string& url);   // "OPTIONS"（避免与 Options 类型重名）
  Result<Response> Send(const Request& req);

  void Close();
  ~Client();
  Client(Client&&) noexcept;              // 可移动
  Client& operator=(Client&&) noexcept;
  // 不可拷贝
};
```

## 行为要点

- **创建即校验**：`Create` 调用 `Options::Validate()`（含 `Tls::Validate()`），非法配置返回 `Error(kInvalidArgument)`，不抛异常。
- **快捷方法**：`Post(url, body)` 等内部经 `Request::Builder` 补默认 `Content-Type: text/plain`；需要自定义 header/content-type 时用 Builder + `Send(req)`。
- **请求级覆盖**：`Request::Timeout(ms)` 优先于 client 级超时（见 http-config-mapping.md）。
- **线程安全**：并发 `Send` 经引擎互斥锁串行化。
- **Close 后调用 Send**：返回 `Error(kInvalidState)`。
- **无库内自动重试**：上层可基于 `res.error().code()` 自行实现（见 retry-policy.md）。

## 使用示例

```cpp
auto client = cpp_network::http::Client::Create(opts);   // opts: Options 链式配置
if (!client.ok()) { /* client.error() */ }
auto res = client->Get("https://example.com/");
if (res.ok() && res->ok()) { /* res->status(), res->body() */ }
```

完整示例见 `src/examples/http_demo/`（`sample/` 下为独立 workspace 的外部消费示例）。

## 边界与约束

- v1 仅 HTTP/1.1 同步语义；无异步/Promise 层（ADR-001）。
- 不暴露 libcurl 类型（FR-013）；公共头仅依赖标准库。
