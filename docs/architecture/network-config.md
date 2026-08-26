# NetworkConfig Entity Design

**Branch**: `001-cpp-network-library` | **Date**: 2026-08-26

**对应需求**: FR-004（timeout）、FR-008（重定向）、FR-009（重试）、FR-010（代理）、FR-012（连接池）、FR-019（TLS）

**用户故事**: US3 (P2) — Configure Network Client Settings

**相关设计**: [http-config-mapping.md](http-config-mapping.md)、[retry-policy.md](retry-policy.md)、[tls-config.md](tls-config.md)、[http-client-api.md](http-client-api.md)

## Overview

`NetworkConfig` 是 `HttpClient` 的统一配置实体，经 `HttpClient::Config` 流式构建。所有字段在 `Build()` 时校验，映射到 libcurl 选项见 http-config-mapping.md。

## 实体定义

```cpp
// src/public/include/netlib/network_config.h
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

#include "netlib/tls_config.h"
#include "netlib/netlib_export.h"

namespace netlib {

struct RetryPolicy {   // 见 retry-policy.md
  int max_retries = 0;                    // 0 = 不重试
  std::chrono::milliseconds retry_delay{100};
  enum class Condition { kNone, kNetworkError, kNetworkErrorOr5xx };
  Condition condition = Condition::kNone;
};

struct Proxy {
  std::string host;
  uint16_t port = 8080;
};

struct NetworkConfig {
  // 超时（ms）
  std::chrono::milliseconds connect_timeout{10000};
  std::chrono::milliseconds read_timeout{30000};
  std::chrono::milliseconds write_timeout{30000};
  std::chrono::milliseconds total_timeout{0};   // 0 = 不限

  // 重定向
  bool follow_redirects = true;
  int max_redirects = 20;

  // 重试
  RetryPolicy retry_policy;

  // 代理（无 = 直连）
  std::optional<Proxy> proxy;

  // TLS
  TlsConfig tls_config;

  // 连接池（委托 libcurl）
  int max_connections_per_host = 5;
  std::chrono::milliseconds keep_alive{120000};

  // 校验（返回 optional<Error>）
  std::optional<Error> Validate() const;
};

}  // namespace netlib
```

## HttpClient::Config 与 NetworkConfig 的关系

`HttpClient::Config` 是**流式构建器**，内部持有 `NetworkConfig` 默认实例并逐项覆盖，`Build()` 时：

```cpp
// src/public/include/netlib/http_client.h（内部语义）
class HttpClient::Config {
 public:
  Config& SetConnectTimeout(std::chrono::milliseconds ms) {
    config_.connect_timeout = ms; return *this;
  }
  // ... 其余同模式
  // TLS 通过 config_.tls_config（嵌套 builder）
  Config& SetTlsConfig(const TlsConfig& tls) { config_.tls_config = tls; return *this; }

  HttpClient Build() const {
    // 1. 校验 config_.Validate()（返回 Result 错误）
    // 2. 构造 HttpClient
  }
 private:
  NetworkConfig config_;
};
```

**设计决策**：`HttpClient::Config` 持有并修改 `NetworkConfig`，`Build()` 校验后移交给 `HttpClient`。`NetworkConfig` 本身对外暴露为只读快照（`const` 使用），不支持构建后再修改。

## 校验规则（NetworkConfig::Validate）

1. **timeout 非负**：connect/read/write/total 均 ≥ 0ms。
2. **max_redirects ≥ 0**。
3. **retry_policy**：`max_retries ≥ 0`；`condition != kNone` 时 `max_retries > 0`。（注：库内不自动重试，该字段供上层读取/校验。）
4. **proxy**：`host` 非空、合法（无空白/`\r`/`\n`）；`port ∈ [1, 65535]`。
5. **max_connections_per_host ≥ 1**。
6. **keep_alive ≥ 0ms**。
7. **tls_config**：委托 `TlsConfig::Build` 校验（见 tls-config.md）。

校验失败 → `Error(kInvalidArgument, message)`，由 `HttpClient::Config::Build` 返回（contracts 错误策略）。

## 默认值策略（与 spec/contracts 对齐）

| 字段 | 默认 | 依据 |
|------|------|------|
| connect_timeout | 10s | 常用实践 |
| read_timeout | 30s | 常用实践 |
| write_timeout | 30s | 常用实践 |
| total_timeout | 不限(0) | 单阶段超时已覆盖 |
| follow_redirects | true | contracts #7 |
| max_redirects | 20 | contracts #7 / libcurl 默认 |
| retry_policy | 不重试 | contracts #8 |
| proxy | 无 | — |
| tls_config | kVerifyPeer | contracts #9 |
| max_connections_per_host | 5 | libcurl 默认 |
| keep_alive | 120s | 常用实践 |

## 生命周期与不可变性

- `NetworkConfig` 一经 `Build()` 提交给 `HttpClient` 即**只读**（`Impl` 持有 const 拷贝）。
- 修改配置需重新构建 `HttpClient`（v1 不支持热更新）。
- 请求级覆盖（`HttpRequest::timeout`）优先级 > `NetworkConfig`（http-config-mapping.md）。

## 边界与约束

- 不做配置热更新 / 动态 reload（YAGNI）。
- 不做 per-host 差异化配置（全局配置 v1）。
- `NetworkConfig` 直接暴露为 struct + 校验方法；builder 语义收敛在 `HttpClient::Config`（避免双 builder）。

## 评审要点

1. `HttpClient::Config` 与 `NetworkConfig` 的关系是否清晰（builder 持有 config，Build 后只读）？
2. 全部字段默认值是否与 contracts 不变量一致？
3. 校验规则是否覆盖 spec US3 三个场景（1s 超时/3 次重试/proxy 路由）？
4. 配置修改需重建 client 的限制是否可接受（v1）？
