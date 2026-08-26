# HTTP 传输生命周期（已实现）

**Branch**: `003-http-implementation` | **Date**: 2026-08-26

**对应需求**: FR-002（同步阻塞）、FR-005（错误映射）、SC-004（连接复用）

**实现位置**: `src/http/engine.cc`（PerformSingle）、`src/http/detail/curl_mapping.cc`、[sync-engine.md](sync-engine.md)、[core-error.md](core-error.md)

## Overview

一次 `Client::Send(Request)` 的完整生命周期。全部步骤同步阻塞调用线程，单次传输内无重试。

## 状态机

```text
Client::Send(req)
  └─ Engine::Send → 加锁
       1. closed_ / multi_ 检查        → kInvalidState
       2. curl_easy_init               → kOutOfMemory
       3. 挂接 Write/Header 回调（内存缓冲）
       4. ApplyEasyOptions             ← Request + Options 全量映射（下表）
          失败                          → kInvalidArgument / kOutOfMemory
       5. curl_multi_add_handle        → kInternalError
       6. 循环 poll(1000ms)+perform，记录 started_at
       7. info_read 取本 easy 的 DONE result
       8. remove_handle
       ├─ 失败：MapCurlError(result, strerror, 超时细分)   ← core-error.md
       └─ 成功：
            status == 0                → kProtocolError("missing HTTP status line")
            构造 Response（全量缓冲）→ Ok
       9. slist_free_all + easy_cleanup + 解锁
```

## 选项应用时机

- **easy 级**：每次 `Send` 新建 easy 时由 `ApplyEasyOptions` 应用。
- **multi 级**：Engine 构造时 `ApplyMultiOptions` 应用一次（`CURLMOPT_MAX_HOST_CONNECTIONS`）。

## 超时映射表

| 配置 | libcurl 选项 | 触发错误码 |
|------|--------------|-----------|
| 请求级 `Request::Timeout(ms)` | `CURLOPT_TIMEOUT_MS`（最高优先级） | `kTotalTimeout` |
| `total_timeout` (>0) | `CURLOPT_TIMEOUT_MS` | `kTotalTimeout` |
| `write_timeout` (>0) | `CURLOPT_TIMEOUT_MS` 兜底上限（无专用写超时选项） | `kWriteTimeout` |
| `read_timeout` (>0) | `CURLOPT_LOW_SPEED_LIMIT=1` + `LOW_SPEED_TIME=⌈read_timeout⌉s`（空闲检测近似） | `kReadTimeout` |
| `connect_timeout` | `CURLOPT_CONNECTTIMEOUT_MS`（覆盖 TCP+TLS 握手阶段） | `kConnectionTimeout` |

同一 `CURLE_OPERATION_TIMEDOUT` 的归属由耗时比较判定（100ms 容差），见 core-error.md。

## 其他映射速览

| 配置 | libcurl 选项 |
|------|--------------|
| follow_redirects / max_redirects | `CURLOPT_FOLLOWLOCATION` / `MAXREDIRS`（默认 true/20） |
| interface / local_address | `CURLOPT_INTERFACE` |
| local_port | `CURLOPT_LOCALPORT` |
| proxy | `CURLOPT_PROXY = "host:port"`（类型用 libcurl 默认 HTTP） |
| keep_alive (>0) | `CURLOPT_TCP_KEEPALIVE=1` + `TCP_KEEPIDLE=⌈keep_alive⌉s` |
| max_connections_per_host | `CURLMOPT_MAX_HOST_CONNECTIONS` |
| TLS 全部字段 | `CURLOPT_SSL_*`，见 tls-config.md |

## 重试与流式

- **库内不重试**：失败直接返回；上层循环可基于 ErrorCode 决定（retry-policy.md）。证书校验失败不应重试。
- **流式分支不存在**：Send 总是等待传输完整结束并全量缓冲（http-response.md「deferred」节）。
