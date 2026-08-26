# NetworkConfig → libcurl Option Mapping Design

**Branch**: `001-cpp-network-library` | **Date**: 2026-08-26

**对应需求**: FR-004（timeout）、FR-008（重定向）、FR-009（重试）、FR-010（代理）、FR-012（连接池）

**用户故事**: US1 (P1) / US3 (P2)

**相关设计**: [network-config.md](network-config.md)、[retry-policy.md](retry-policy.md)、[http-client-api.md](http-client-api.md)、[tls-config.md](tls-config.md)

## Overview

定义 `NetworkConfig`（及请求级覆盖）如何映射为 libcurl 的 CURL/CURLM 选项。该映射是 `HttpClient::Impl::BuildRequest` 的核心，保证一次映射、处处生效（含重试后的新传输）。

## 映射总表

| NetworkConfig 字段 | 目标（CURL/CURLM） | libcurl 选项 | 默认值 |
|--------------------|--------------------|--------------|--------|
| `connect_timeout` | easy | `CURLOPT_CONNECTTIMEOUT_MS` | 10s |
| `read_timeout` | easy | `CURLOPT_TIMEOUT_MS` 语义 + `CURLOPT_LOW_SPEED_LIMIT`/`CURLOPT_LOW_SPEED_TIME`（空闲读超时） | 30s |
| `write_timeout` | easy | `CURLOPT_TIMEOUT_MS` 语义（写方向由低速率检测近似） | 30s |
| `total_timeout` | easy | `CURLOPT_TIMEOUT_MS`（硬性总闸） | 0（不限） |
| `follow_redirects` | easy | `CURLOPT_FOLLOWLOCATION` | true |
| `max_redirects` | easy | `CURLOPT_MAXREDIRS` | 20 |
| `proxy.host` + `proxy.port` | easy | `CURLOPT_PROXY`（`host:port`） | 空（无代理） |
| `proxy.type` | easy | `CURLOPT_PROXYTYPE`（`CURLPROXY_HTTP` 默认） | HTTP |
| `retry_policy` | 传输层 | 非 libcurl 选项；由引擎实现（见 retry-policy.md） | 不重试 |
| `tls_config` | easy | `CURLOPT_SSL_*`（见 tls-config.md） | kVerifyPeer |
| `max_connections_per_host` | multi | `CURLMOPT_MAX_HOST_CONNECTIONS` | 5 |
| `keep_alive` | multi | `CURLMOPT_MAXCONNECTS` + 连接复用语义 | 120s |

## 应用时机

1. **easy 级别选项**（CURLOPT_*）：每个传输创建 easy handle 时应用。
   - 来源优先级（高→低）：请求级覆盖（`HttpRequest::timeout`）→ `NetworkConfig` → libcurl 默认。
2. **multi 级别选项**（CURLMOPT_*）：`SyncEngine` 创建共享 `CURLM*` 时应用一次；`HttpClient` 生命周期内固定。

## 超时映射细节

- `total_timeout`：直接 `CURLOPT_TIMEOUT_MS`，整个传输（含重定向、重试内的单次传输）硬性上限。**注意**：libcurl 的 `CURLOPT_TIMEOUT_MS` 是"整个 easy 传输"的超时，与"总请求超时"语义一致（重试会新建 easy，故每次传输重新起算；若需"重试链总超时"，由引擎累计时间实现，见评审要点 3）。
- `read_timeout`：以"低速率超时"实现空闲读超时：
  ```text
  CURLOPT_LOW_SPEED_LIMIT = 1（字节/秒）
  CURLOPT_LOW_SPEED_TIME  = read_timeout 秒
  ```
  当连续 read_timeout 内无数据（<1B/s）→ `CURLE_OPERATION_TIMEDOUT` → 引擎按阶段判定 `kReadTimeout`。
- `connect_timeout`：`CURLOPT_CONNECTTIMEOUT_MS`，覆盖 TCP 连接与 TLS 握手阶段。

## 代理细节

- `CURLOPT_PROXY = "host:port"`（如 `"proxy.example.com:8080"`）。
- `CURLOPT_PROXYTYPE = CURLPROXY_HTTP`（v1 仅 HTTP 代理，SOCKS 留 polish）。
- HTTPS 代理（`CURLPROXY_HTTPS`）v1 不做；`https://` 目标经 HTTP 代理用 CONNECT 隧道（libcurl 默认行为）。

## 连接池细节（委托 libcurl）

- `CURLMOPT_MAX_HOST_CONNECTIONS = max_connections_per_host`（默认 5）：限制单 host 并发连接数，超出排队。
- `CURLMOPT_MAXCONNECTS`（默认 5）：缓存的最大连接总数，配合 `keep_alive` 控制复用窗口。
- 连接空闲关闭时机由 libcurl 内部控制（keep-alive 超时），库只暴露调优旋钮。

## 错误映射联动

- 超时类错误（`CURLE_OPERATION_TIMEDOUT`）在引擎按**当前阶段**判定具体 ErrorCode：
  - 处于连接阶段 → `kConnectionTimeout`；
  - 处于读等待 → `kReadTimeout`；
  - 处于写等待 → `kWriteTimeout`；
  - 总超时到达 → `kTotalTimeout`。
- 引擎在每次传输的 easy 上下文中记录 `stage_`（connecting/sending/reading）用于判定。

## 边界与约束

- 请求级覆盖目前仅 timeout（v1）；headers/body/方法见 HttpRequest。
- 单次传输内 `read/write` 与 `total` 超时的交互：`CURLOPT_TIMEOUT_MS` 优先级高于低速检测（先到者胜）。
- 不做"每请求连接池隔离"（全局共享池）。
- **重试**：库内单次传输；`total_timeout` 只约束单次传输，不约束上层重试链（重试由上层循环，总超时由上层自行累计）。

## 评审要点

1. `total_timeout` 仅约束单次传输（重试由上层累计）的语义是否明确？
2. read_timeout 用低速率检测近似是否可接受（精度 vs libcurl 限制）？
3. 阶段判定（connecting/sending/reading）是否可靠区分各类超时？
4. 代理仅 HTTP 类型的限制是否明确文档化？
