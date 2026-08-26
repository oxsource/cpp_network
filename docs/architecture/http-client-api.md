# HttpClient API Design (axios-Inspired, 同步版)

**Branch**: `001-cpp-network-library` | **Date**: 2026-08-26（同步重构版）

**对应需求**: FR-001（HttpClient 接口）、FR-018（修订：同步阻塞）、FR-020（axios 风格）、FR-011（并发安全）

**用户故事**: US1 (P1) — Send HTTP Request and Receive Response

**相关设计**: [http-request.md](http-request.md)、[http-response.md](http-response.md)、[sync-engine.md](sync-engine.md)、[network-config.md](network-config.md)、[contracts/public-api.md](../../specs/001-cpp-network-library/contracts/public-api.md)

## Overview

设计 `HttpClient` 的公共 API。风格对标 axios 的易用性（简单 per-verb 方法、统一配置、不可变值类型），但为**同步阻塞**：每个方法直接返回 `Result<HttpResponse>`（或含错误）。库内无 Promise/回调/事件循环。

## 公共 API 定义

```cpp
// src/public/include/netlib/http_client.h
#pragma once

#include <chrono>
#include <memory>
#include <string>

#include "netlib/http_request.h"
#include "netlib/http_response.h"
#include "netlib/network_config.h"
#include "netlib/result.h"
#include "netlib/netlib_export.h"

namespace netlib {

class HttpClient {
 public:
  // 流式配置构建器（链式）。最终 Build() 创建 HttpClient。
  class Config {
   public:
    Config& SetConnectTimeout(std::chrono::milliseconds ms);
    Config& SetReadTimeout(std::chrono::milliseconds ms);
    Config& SetWriteTimeout(std::chrono::milliseconds ms);
    Config& SetTotalTimeout(std::chrono::milliseconds ms);
    Config& SetRetryPolicy(const RetryPolicy& policy);
    Config& SetProxy(const std::string& host, uint16_t port);
    Config& SetFollowRedirects(bool follow);
    Config& SetMaxRedirects(int n);
    Config& SetTlsConfig(const TlsConfig& config);
    Config& SetMaxConnectionsPerHost(int n);
    Config& SetKeepAlive(std::chrono::milliseconds ms);

    // 校验配置并构建；失败返回 Result 错误。
    Result<HttpClient> Build() const;
  };

  // —— axios 风格同步方法（全部阻塞调用线程，返回 Result<HttpResponse>）——
  Result<HttpResponse> Get(const std::string& url);
  Result<HttpResponse> Get(const HttpRequest& req);
  Result<HttpResponse> Post(const std::string& url, const std::string& body);
  Result<HttpResponse> Post(const std::string& url,
                            const std::string& body,
                            const std::string& content_type);
  Result<HttpResponse> Post(const HttpRequest& req);
  Result<HttpResponse> Put(const std::string& url, const std::string& body);
  Result<HttpResponse> Delete(const std::string& url);
  Result<HttpResponse> Patch(const std::string& url, const std::string& body);
  Result<HttpResponse> Head(const std::string& url);
  Result<HttpResponse> Options(const std::string& url);

  // 通用入口：接受任意 HttpRequest。
  Result<HttpResponse> Send(const HttpRequest& req);

  // 关闭连接池（线程安全）。
  void Close();

  ~HttpClient();

  HttpClient(const HttpClient&) = delete;
  HttpClient& operator=(const HttpClient&) = delete;

 private:
  friend class Config;
  explicit HttpClient(const Config& config);
  class Impl;
  std::shared_ptr<Impl> impl_;
};

}  // namespace netlib
```

## axios 映射与 API 约定

| axios 用法 | 本库用法 |
|------------|----------|
| `axios.get(url)` | `client.Get(url)` → `Result<HttpResponse>` |
| `axios.post(url, data)` | `client.Post(url, body)` |
| `axios({ method, url, headers, data, timeout })` | `client.Send(HttpRequest::Builder()...)` |
| 拦截器 / defaults | `HttpClient::Config` 统一配置（重试由上层实现） |

### 语义细节

1. **URL 快捷方法**：`Get(url)` 等价于 `Send(Builder().Method(kGet).Url(url).Build())`。
2. **Content-Type 默认值**：`Post(url, body)` 默认 `text/plain`；带 `content_type` 重载或 `HttpRequest::JsonBody()` 覆盖。
3. **超时覆盖优先级**：请求级 timeout > client 级 `Config` timeout（http-config-mapping.md）。
4. **重试**：**库内不自动重试**（同步单次传输）。上层如需重试，循环调用 `Send` 并按 `RetryPolicy` 判定（retry-policy.md）。
5. **重定向**：默认 `follow_redirects=true`、`max_redirects=20`，由 libcurl 内部处理。

## 并发与生命周期

- **线程安全**：`Get/Post/Send` 可从任意线程并发调用；`Impl` 内部 `SyncEngine` 用 mutex 串行化进共享 CURLM（sync-engine.md）。
- **Close()**：关闭连接池；此后 `Send` 返回 `Result<Error(kInvalidState)>`。
- **无 Executor**：库不接收也不使用任何调度器；阻塞语义由调用线程承担。

## 内部结构（Impl）

```text
HttpClient
 └── Impl
     ├── config_  : NetworkConfig（已校验）
     └── engine_  : SyncEngine（拥有共享 CURLM*）
```

`Send` 流程（详见 sync-engine.md）：

```text
Send(req)
 → 校验 HttpRequest（非法 → Result<Error(kInvalidArgument)>）
 → engine_.Send(req)  // 阻塞，返回 Result<HttpResponse>
 → 返回
```

## 边界与约束

- 不提供异步入口（同步阻塞是 v1 唯一形态）；上层用线程池/协程包出异步。
- 不提供取消（阻塞期间无法中断）。
- 不暴露 `CURL*`/`CURLM*` 到公共 API。

## 评审要点

1. 所有方法是否都同步返回 `Result<HttpResponse>`（无异步/回调路径）？
2. `Build()` 失败与 `Send` 失败的错误表示（Result）是否一致？
3. 请求级 timeout 覆盖 client 级配置的优先级是否清晰？
4. `Close()` 后继续 `Send` 返回 `kInvalidState` 的语义是否明确？
