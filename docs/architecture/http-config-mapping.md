# Options/Request → libcurl 选项映射（已实现）

**Branch**: `003-http-implementation` | **Date**: **2026-08-26**

**对应需求**: FR-004、FR-008、FR-010、FR-012

**实现位置**: `src/http/detail/curl_mapping.cc`（ApplyEasyOptions / ApplyMultiOptions / EffectiveHardTimeout）

## 映射总表（easy 级）

| 来源字段 | libcurl 选项 | 默认 | 备注 |
|--------------------|--------------|--------|------|
| `Request::timeout()` | `CURLOPT_TIMEOUT_MS` | — | 最高优先级；触发 `kTotalTimeout` |
| `total_timeout` (>0) | `CURLOPT_TIMEOUT_MS` | 0 不限 | 触发 `kTotalTimeout` |
| `write_timeout` (>0) | `CURLOPT_TIMEOUT_MS` 兜底 | 30s | 无专用写超时选项，作硬上限；触发 `kWriteTimeout` |
| `read_timeout` (>0) | `CURLOPT_LOW_SPEED_LIMIT=1` + `CURLOPT_LOW_SPEED_TIME=⌈ms⌉s` | 30s | 空闲检测近似；触发 `kReadTimeout` |
| `connect_timeout` | `CURLOPT_CONNECTTIMEOUT_MS` | 10s | 覆盖 TCP+TLS 握手；触发 `kConnectionTimeout` |
| `follow_redirects` / `max_redirects` | `CURLOPT_FOLLOWLOCATION` / `MAXREDIRS` | true / 20 | |
| `proxy.host:port` | `CURLOPT_PROXY` | 无 | 类型用 libcurl 默认（HTTP），未显式设 PROXYTYPE |
| `keep_alive` (>0) | `CURLOPT_TCP_KEEPALIVE=1` + `TCP_KEEPIDLE=⌈ms⌉s` | 120s | TCP 层探测；HTTP 复用由 libcurl 连接缓存负责 |
| `interface` / `local_address` | `CURLOPT_INTERFACE` | — | 二者互斥（interface 优先） |
| `local_port` | `CURLOPT_LOCALPORT` | — | |
| TLS 全部 | `CURLOPT_SSL_*` | kVerifyPeer | 见 tls-config.md |
| method (≠HEAD) | `CURLOPT_CUSTOMREQUEST` | GET | HEAD 用 `CURLOPT_NOBODY=1` |
| body | `CURLOPT_POSTFIELDS` + `POSTFIELDSIZE_LARGE` | — | 仅 has_body 时设置 |
| headers | `CURLOPT_HTTPHEADER`（curl_slist） | — | 保序 |

## 映射总表（multi 级）

| 来源字段 | CURLM 选项 | 默认 |
|--------------------|--------------|--------|
| `max_connections_per_host` | `CURLMOPT_MAX_HOST_CONNECTIONS` | 5 |

未设置 `CURLMOPT_MAXCONNECTS`（libcurl 按需扩展连接缓存）。

## 超时解析优先级（EffectiveHardTimeout）

```text
request.timeout()            → TIMEOUT_MS, error code kTotalTimeout
else total_timeout() > 0     → TIMEOUT_MS, error code kTotalTimeout
else write_timeout() > 0     → TIMEOUT_MS, error code kWriteTimeout
else                         → not set, TIMEDOUT classified as kConnectionTimeout
```

注意：write_timeout 有默认值 30s，因此**默认配置下所有传输有 30s 硬上限**；长传输需显式调大或置 0。

引擎在失败路径按耗时（100ms 容差）区分硬超时与低速率读超时，见 core-error.md「CURLE_OPERATION_TIMEDOUT 细分」。

## 应用时机

- easy 级：每次 `Send` 新建 easy handle 时应用一次。
- multi 级：Engine 构造时应用一次，Client 生命周期内固定。

## 边界与约束

- v1 代理仅 HTTP 类型（SOCKS/HTTPS 代理留 polish）；HTTPS 目标经 CONNECT 隧道。
- 单次传输内 `TIMEOUT_MS` 先到者胜于低速检测。
- 不做 per-request 连接池隔离（全局共享）。
