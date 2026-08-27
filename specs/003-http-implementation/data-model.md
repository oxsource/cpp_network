# Data Model: 设计实现并验证 HTTP

**Branch**: `003-http-implementation` | **Date**: 2026-08-26 | **Spec**: [spec.md](spec.md)

## Entities

### Client（原 HttpClient）

- **Purpose**: 同步请求入口；持 Options + 传输引擎。
- **属性**:
  - `options: Options` — 超时/重定向/网卡/连接池/TLS 配置。
  - `engine_: Engine` — 共享 CURLM（mutex 保护）。
- **关系**: 由 `Options` 构建；调用 `Send` 返回 `Result<Response>`。
- **生命周期**: 构建后复用；`Close()` 后 `Send` 返回 `kInvalidState`。
- **校验**: Options 校验通过才可构建。

### Request（原 HttpRequest）

- **Purpose**: 不可变请求值；Builder 构造。
- **属性**: `method: Method`、`url: string`、`headers: Headers`、`body: optional<string>`、`timeout: optional<Duration>`。
- **校验**: URL 绝对合法、无 CRLF 注入、method-body 约束、timeout 非负。
- **关系**: 经 `Client::Send` 执行。

### Response（原 HttpResponse）

- **Purpose**: 不可变响应值。
- **属性**: `status: int`、`status_text: string`、`headers: Headers`、`body: string`（或 `stream` 句柄）、`effective_url: string`。
- **关系**: `Client::Send` 产出。
- **状态**: 缓冲模式（完整 body）或流式模式（>8MB 经 `stream()`）。

### Options（统一配置）

- **Purpose**: Client 配置。
- **属性**:
  - 超时: `connect/read/write/total`。
  - 重定向: `follow_redirects`、`max_redirects`。
  - **网卡**: `interface: optional<string>`、`local_address: optional<string>`、`local_port: optional<int>`。
  - 代理: `proxy: optional<Proxy>`。
  - 连接池: `max_connections_per_host`、`keep_alive`。
  - TLS: `tls: Tls`。
- **校验**: timeout 非负、max_redirects ≥0、interface/local_address 合法、max_connections ≥1。

### Tls（证书配置，原 TlsConfig）

- **Purpose**: TLS 配置。
- **属性**:
  - `verify_mode: VerifyMode`（kVerifyPeer/kSkipVerification）。
  - `ca_pem: optional<string>`、`ca_file: optional<string>`（自定义 CA，内存或文件）。
  - `client_cert: optional<string>`、`client_key: optional<string>`（mTLS，PEM 或文件路径）。
  - `sni: optional<string>`。
- **校验**: 合法 PEM、mTLS 证书/私钥成对、SNI 无 CRLF。
- **关系**: 映射 CURLOPT_SSL_*（见 research Decision 6）。

### Engine（同步传输引擎，原 SyncEngine）

- **Purpose**: 内部同步驱动。
- **属性**: `multi_: CURLM*`、`mu_: mutex`、`closed_: bool`。
- **关系**: 被 Client 调用；`curl_multi_poll` 阻塞驱动。
- **状态**: 活跃 → 关闭（Close/析构）。

### Result<T> / Error

- **Purpose**: 统一错误表示。
- **属性**: `Error.code: ErrorCode`、`Error.message: string`。
- **关系**: `Client::Send` 返回 `Result<Response>`；错误映射表沿用 001 core-error.md。

### Method（HTTP 方法枚举）

- **Purpose**: `Get/Post/Put/Delete/Patch/Head/Options`。

## 状态转换

### 请求生命周期（同步）

```text
Send(Request) → validate → lock shared CURLM → curl_multi_poll blocks → CURLMSG_DONE
            → Result<Response>::Ok | Result<Error>
```

### Engine 生命周期

```text
Build (create CURLM) → Active (requests reuse the connection pool) → Close (cleanup) → subsequent Send returns kInvalidState
```

## 校验规则汇总

- Request: URL 绝对合法、无 CRLF 注入、GET/HEAD 无 body、timeout ≥0。
- Options: timeout ≥0、max_redirects ≥0、interface/local_address 合法、max_connections_per_host ≥1。
- Tls: 合法 PEM、mTLS 成对、SNI 无 CRLF。
