# HTTP Transfer Lifecycle Design（同步）

**Branch**: `001-cpp-network-library` | **Date**: 2026-08-26（同步重构版）

**对应需求**: FR-001（请求/响应）、FR-008（重定向）、FR-009（重试，上层实现）

**用户故事**: US1 (P1) — Send HTTP Request and Receive Response

**相关设计**: [sync-engine.md](sync-engine.md)、[http-client-api.md](http-client-api.md)、[http-response.md](http-response.md)

## Overview

描述一次 HTTP 传输从 `HttpClient::Send` 到返回 `Result<HttpResponse>` 的完整同步生命周期，以及重定向、超时、并发在其中的流转。**无 Promise、无重试（库内单次传输）**。

## 状态机（单次传输）

```text
Send(req)
 → 校验(HttpRequest) → Result<Error> | 
 → 加锁进共享 CURLM（sync-engine.md）
 → curl_multi_poll 阻塞驱动直到本 easy CURLMSG_DONE
 → 缓冲模式: 提取完整响应 → 返回 Result<HttpResponse>
 → 流式模式: 头读完返回 → 返回 Result<HttpResponse>(带 body_stream)
 → 失败: CURLcode → ErrorCode → 返回 Result<Error>
```

## 各阶段详细设计

### 1. 校验

`HttpClient::Send(HttpRequest)` 被调用：

- 校验 HttpRequest（http-request.md 校验规则）。失败 → 立即返回 `Result<Error(kInvalidArgument)>`，不进入引擎。
- 校验通过 → 调用 `SyncEngine::Send(req)`。

### 2. 进入共享 CURLM

- 加锁 `mu_`（sync-engine.md）。
- 创建 CURL easy handle 并应用选项：
  - 全局：`HttpClient::Config` 映射（http-config-mapping.md）
  - 请求级：`HttpRequest` 字段 + timeout 覆盖
  - TLS：`TlsConfig` → `CURLOPT_SSL_*`（tls-config.md）
- `curl_multi_add_handle(multi_, easy)`。
- 写回调绑定：`CURLOPT_WRITEFUNCTION`（body 收集，见 http-response.md 缓冲/流式判定）。

### 3. 阻塞驱动

```cpp
int running = 0;
do {
  curl_multi_poll(multi_, /*extra_fds=*/nullptr, /*nfds=*/0,
                  /*timeout_ms=*/剩余 total_timeout 或 -1, &numfds);
  curl_multi_perform(multi_, &running);
  // 检查 curl_multi_info_read 是否本 easy 完成（CURLMSG_DONE）
} while (本 easy 未完成 && 未超时);
```

- 锁内驱动所有已入列 easy（含其他线程的请求）的 IO，实现连接池复用。
- 重定向：`CURLOPT_FOLLOWLOCATION` 开启时由 libcurl 内部跟随，直到 `CURLOPT_MAXREDIRS`（默认 20）。超限 → `CURLE_TOO_MANY_REDIRECTS` → `kTooManyRedirects`。

### 4. 完成（缓冲模式）

- `rc == CURLE_OK`：
  - 读取 `CURLINFO_RESPONSE_CODE`、headers、body、`CURLINFO_EFFECTIVE_URL`；
  - 构造 `HttpResponse` → `Result<HttpResponse>::Ok(resp)`。
- `rc != CURLE_OK` → 映射 `Error`（core-error.md）→ `Result<Error>`。

### 5. 流式分支

- 当判定 body 将超阈值（http-response.md），在读完头后**返回** `Result<HttpResponse>`（挂 `BodyStream` 句柄），body 未读完；用户后续同步 `Read()`。
- 返回前保持 easy 与读上下文存活（挂到 BodyStream impl）。

### 6. 清理

- `curl_multi_remove_handle(multi_, easy)` + `curl_easy_cleanup(easy)`。
- 解锁 `mu_`。
- 返回 Result。

## 失败与重试

- 库内**不重试**（单次传输）。失败 → 返回 `Result<Error>`。
- 上层重试：调用方循环调用 `Send`，按 `RetryPolicy` 判定错误类型是否命中（retry-policy.md），重试之间自行 sleep/调度。
- 重定向：libcurl 内部处理，不计入上层重试计数。

## 超时语义（组合）

| 配置 | 作用阶段 | libcurl 选项 | 超时 → ErrorCode |
|------|----------|--------------|------------------|
| connect_timeout | TCP/TLS 连接 | `CURLOPT_CONNECTTIMEOUT_MS` | `kConnectionTimeout` |
| read_timeout | 读空闲 | `CURLOPT_LOW_SPEED_*` | `kReadTimeout` |
| write_timeout | 写等待 | `CURLOPT_TIMEOUT_MS` 语义近似 | `kWriteTimeout` |
| total_timeout | 整个传输 | `CURLOPT_TIMEOUT_MS` + `curl_multi_poll` 超时 | `kTotalTimeout` |

映射细节见 http-config-mapping.md。

## 并发与资源

- 多线程并发 `Send`：经 `mu_` 串行化进共享 CURLM（sync-engine.md）；调用线程各自阻塞。
- 无内部线程、无回调、无事件循环。
- 大 body 流式下，easy/读上下文生命周期延长至 BodyStream 析构。

## 评审要点

1. 流式分支的返回时机（头读完）与调用方 `Read()` 的衔接是否明确？
2. 重试由上层实现后，库内无重试的状态机是否完整？
3. 锁内驱动所有 easy 的连接池复用是否在并发下成立（sync-engine.md 评审）？
4. 超时映射是否覆盖连接/读/写/总四个维度且优先级正确？
