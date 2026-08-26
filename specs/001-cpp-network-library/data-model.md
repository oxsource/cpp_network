# Data Model: C++ Cross-Platform Network Library

**Branch**: `001-cpp-network-library` | **Date**: 2026-08-26 | **Spec**: [spec.md](spec.md)

## Entities

### HttpClient

- **Purpose**: Main entry point for making network requests. Holds configuration and the shared libcurl multi handle.
- **Attributes**:
  - `config: NetworkConfig` — client-wide settings (timeouts, retry, proxy, TLS).
  - `curl_multi: CurlMultiHandle` — shared libcurl multi handle（owns connections, mutex 保护）。
  - `mu_: mutex` — 串行化多线程并发 `Send` 对 CURLM 的访问。
- **Relationships**: owns `NetworkConfig`, `CurlMultiHandle`; 无线程/无事件循环（同步 API）。
- **Lifecycle**: constructed via fluent `Config`; reuse across many requests; thread-safe for concurrent request dispatch (multi-thread 并发 Send 经锁串行化)。

### HttpRequest

- **Purpose**: Immutable description of an outgoing HTTP request.
- **Attributes**:
  - `method: Method` — GET, POST, PUT, DELETE, PATCH, HEAD, OPTIONS.
  - `url: Url` — parsed URL (scheme, host, port, path, query).
  - `headers: Headers` — case-insensitive header map.
  - `body: optional<Body>` — optional request body (string, bytes, or stream).
  - `timeout: Timeout` — optional per-request override.
- **Validation**: URL must be valid and absolute; headers must not contain CRLF injection; body present only for methods that allow it.

### HttpResponse

- **Purpose**: Immutable representation of an incoming HTTP response.
- **Attributes**:
  - `status_code: int` — HTTP status code.
  - `status_text: string` — reason phrase.
  - `headers: Headers` — response headers.
  - `body: Body` — response body (buffered or streaming).
- **Relationships**: produced by `HttpClient` request execution.

### NetworkConfig

- **Purpose**: Configuration for `HttpClient`.
- **Attributes**:
  - `connect_timeout: Duration`
  - `read_timeout: Duration`
  - `write_timeout: Duration`
  - `total_timeout: Duration`
  - `retry_policy: RetryPolicy` — max retries, retry delay, retry condition.
  - `proxy: optional<Proxy>` — HTTP proxy address.
  - `follow_redirects: bool`
  - `max_redirects: int`
  - `tls_config: TlsConfig` — CA certs, verify mode, custom verification.
  - `pool_config: PoolConfig` — max connections per host, keep-alive duration.
- **Validation**: timeouts must be non-negative; max_retries >= 0; proxy must be a valid host:port.

### RetryPolicy

- **Purpose**: Defines retry behavior.
- **Attributes**:
  - `max_retries: int`
  - `retry_delay: Duration`
  - `retry_condition: enum` — e.g., on network error only, or also on 5xx.
- **State transitions**: initial attempt → on failure, if retry_condition matches and attempts < max_retries → retry with delay → else fail.

### TlsConfig

- **Purpose**: TLS-specific settings, mapped to libcurl SSL options.
- **Attributes**:
  - `verify_mode: VerifyMode` — verify peer / skip verification (→ `CURLOPT_SSL_VERIFYPEER`/`CURLOPT_SSL_VERIFYHOST`).
  - `ca_certificates: optional<vector<Certificate>>` — custom CA bundle (→ `CURLOPT_CAINFO`).
  - `client_certificate: optional<Certificate>` — mutual TLS (→ `CURLOPT_SSLCERT`/`CURLOPT_SSLKEY`).
  - `sni_hostname: optional<string>` — SNI override.
- **Validation**: verify_mode required; CA certs must be PEM/DER parseable.
- **Note**: The actual TLS backend (OpenSSL on host, BoringSSL on Android) is selected when libcurl is built (via Bazel `select()`). No runtime `TlsAdapter` interface is required.

### ConnectionPool (delegated to libcurl)

- **Purpose**: Reuse persistent TCP/TLS connections. **Owned and managed internally by libcurl** (`CURLMOPT_MAX_HOST_CONNECTIONS`, `CURLMOPT_MAXCONNECTS`)，经共享 CURLM 在同步请求间复用。
- **Attributes** (exposed via `NetworkConfig`):
  - `max_connections_per_host: int` → `CURLMOPT_MAX_HOST_CONNECTIONS`.
  - `keep_alive_duration: Duration` → libcurl connection reuse timeout.
- **State transitions** (internal to libcurl): idle ↔ in-use → closed (keep-alive expiry | error | shutdown).

### （已移除）Executor / Promise

- 同步 API 重构后，`Executor`、`Promise`、`WatchFd` 实体**不再存在**。库内无异步抽象；事件/流程/线程由上层控制（见 research.md Decision 1）。

## State Transitions

### Request Lifecycle（同步）

```text
Send(req) → 校验 → 加锁进共享 CURLM → curl_multi_poll 阻塞等待该请求完成
        → Complete(Result<HttpResponse>) | Failed(Result<Error>)
```

- **Retry**：库内**不自动重试**；由上层循环调用 `Send` 实现（RetryPolicy 仅承载配置）。
- **Redirect**：由 libcurl 内部处理（`CURLOPT_FOLLOWLOCATION`/`CURLOPT_MAXREDIRS`）。
- **Timeout**：由 libcurl 选项处理（映射见 http-config-mapping.md）。
- **线程**：多线程并发 `Send` 经 mutex 串行化进共享 CURLM，连接池复用；`curl_multi_poll` 阻塞调用线程。

### Connection Lifecycle (internal to libcurl)

```text
Idle ↔ In-Use → Closed (keep-alive expiry | error | shutdown)
```

Managed entirely by libcurl's multi interface. The library exposes tuning knobs via `NetworkConfig` only.

## Validation Rules Summary

- Headers are case-insensitive; duplicates allowed for set-cookie style, merged otherwise.
- URLs must be absolute with valid scheme (`http`/`https` for v1).
- Timeouts and retry counts must be non-negative.
- TLS verify_mode must be explicitly set in `TlsConfig`.
