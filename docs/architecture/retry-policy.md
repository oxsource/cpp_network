# Retry Policy Design（上层实现；v1 库内未交付 RetryPolicy 类型）

> **状态（2026-08-26，spec 003 实现核对）**：库内不自动重试，且 `Options` 未提供 RetryPolicy 字段。本文保留为上层实现参考模式；符号名以实际 `cpp_network::http` 命名体系为准。

**Branch**: `001-cpp-network-library` | **Date**: 2026-08-26

**对应需求**: FR-009（可配置重试策略）

**用户故事**: US3 (P2) — Configure Network Client Settings

**相关设计**: [network-config.md](network-config.md)、[http-transfer-lifecycle.md](http-transfer-lifecycle.md)、[core-error.md](core-error.md)、[core-executor.md](core-executor.md)

## Overview

定义 `RetryPolicy` 配置语义：重试次数、重试延迟、重试条件。**同步重构后，库内不自动重试**——`Send` 是单次传输；重试由**上层**循环调用 `Send` 并按本策略判定实现。`RetryPolicy` 作为配置承载（供上层读取/校验）。

## 类型定义

```cpp
// src/public/include/netlib/network_config.h（RetryPolicy 定义见 network-config.md）
struct RetryPolicy {
  int max_retries = 0;                     // 0 = 不重试（默认）
  std::chrono::milliseconds retry_delay{100};
  enum class Condition {
    kNone,              // 不重试
    kNetworkError,      // 仅网络类错误重试（DNS/连接/超时等，见下）
    kNetworkErrorOr5xx, // 网络错误 + 5xx 状态码重试
  };
  Condition condition = Condition::kNone;
};
```

## 上层重试实现（调用方模式）

```cpp
// 上层（调用方）实现重试的参考模式：
Result<HttpResponse> RetrySend(HttpClient& client, const HttpRequest& req,
                               const RetryPolicy& policy) {
  int attempts = 0;
  while (true) {
    auto res = client.Send(req);
    if (res.ok()) {
      if (policy.condition == RetryPolicy::Condition::kNetworkErrorOr5xx &&
          res->status_code() >= 500 && attempts < policy.max_retries) {
        attempts++; std::this_thread::sleep_for(policy.retry_delay); continue;
      }
      return res;
    }
    if (ShouldRetry(res.error().code(), policy.condition) &&
        attempts < policy.max_retries) {
      attempts++; std::this_thread::sleep_for(policy.retry_delay); continue;
    }
    return res;
  }
}
```

## 重试条件判定（kNetworkError 命中表）

| ErrorCode | 是否重试 |
|-----------|----------|
| `kDnsResolutionFailed` | ✅ |
| `kConnectionRefused` | ✅ |
| `kConnectionClosed` | ✅ |
| `kConnectionTimeout` | ✅ |
| `kReadTimeout` | ✅ |
| `kWriteTimeout` | ✅ |
| `kTotalTimeout` | ❌（整体超时不重试） |
| `kTlsHandshakeFailed` | ❌ |
| `kCertificateVerificationFailed` | ❌（安全原因） |
| `kProtocolError` / `kMalformedResponse` | ❌ |
| `kTooManyRedirects` / `kInvalidArgument` / `kInvalidState` | ❌ |

kNetworkErrorOr5xx 额外命中：`status_code ∈ {500, 502, 503, 504}`。

## 线程与调度

- 重试延迟的 sleep/调度由**上层**实现（可用线程池/协程/事件循环调度，而非阻塞 sleep）。
- 库内无重试状态机、无重试计数器（每个 `Send` 独立单次传输）。

## 测试/验收场景对照（spec US3 场景 2）

| 配置 | 行为（上层实现） |
|------|------|
| `RetryPolicy{max_retries=3, retry_delay=100ms, condition=kNetworkError}` | 上层循环 Send：首次连接失败 → 重试至多 3 次（总计 4 次尝试）；全失败 → 返回最后一次错误 |

## 边界与约束

- 不做指数退避（v1 固定延迟；上层可按需扩展）。
- 幂等性由调用方负责。
- 库不强制重试（默认 `kNone` 即单次传输）。

## 评审要点

1. `kTotalTimeout`/`kTlsHandshakeFailed`/`kCertificateVerificationFailed` 默认不重试的安全考量是否成立？
2. `attempts_` 对物理传输计数、重定向不计入的语义是否清晰？
3. 5xx 重试是否仅在 `kNetworkErrorOr5xx` 显式开启（默认不重试，contracts #8）？
4. total_timeout 是否约束整个重试链（而非单次传输）？
