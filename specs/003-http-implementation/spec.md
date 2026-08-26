# Feature Specification: 设计实现并验证 HTTP

**Feature Branch**: `003-http-implementation`

**Created**: 2026-08-26

**Status**: Draft

**Input**: User description: "设计实现并验证http"

**类命名约定**: 简化优雅——`Client`（HttpClient）、`Request`（HttpRequest）、`Response`（HttpResponse）、`Options`（NetworkConfig/TlsConfig 统一配置）、`Method`（HttpMethod）、`Error`/`Result`。公共命名空间 `netlib`。

## User Scenarios & Testing *(mandatory)*

### User Story 1 - 发送 HTTP 请求并接收响应 (Priority: P1)

一个 C++ 开发者使用 netlib 库发送 HTTP 请求。他们通过 `Client` 的同步 API 发送 GET/POST 请求到本地或远程服务器，直接获得 `Response`（状态码、headers、body）。请求阻塞调用线程直至完成，错误以 `Result` 返回而非崩溃。此功能在 macOS/Linux 上可用，TLS 使用 OpenSSL。

**Why this priority**: HTTP 请求/响应是库的核心价值。没有可用的 HTTP 传输，库无实际用途。

**Independent Test**: 可以独立验证——本地启动一个 HTTP 测试服务器，发送 GET/POST 请求，验证状态码/headers/body 正确返回。

**Acceptance Scenarios**:

1. **Given** 本地 HTTP 服务器监听 localhost:8080 且返回 200 + body "Hello World"，**When** 开发者通过 `Client::Get("http://localhost:8080/")` 发送请求，**Then** 返回的 `Response` 状态码为 200，`body()` 为 "Hello World"，且调用阻塞直至完成。

2. **Given** 本地 HTTP 服务器返回 404 且带 header `X-Custom: value`，**When** 开发者发送 GET 请求，**Then** 状态码为 404，`GetHeader("X-Custom")` 返回 "value"。

3. **Given** 本地 HTTP 服务器接受 POST，**When** 开发者发送 POST + JSON body `{"key":"value"}`（Content-Type: application/json），**Then** 服务器收到的请求体与 Content-Type 正确，响应状态码为 200。

---

### User Story 2 - HTTPS 请求（OpenSSL TLS + 证书配置）(Priority: P2)

开发者发送 HTTPS 请求到远程服务器（如 https://httpbin.org）。TLS 握手由 OpenSSL 经 libcurl 完成，证书默认校验（kVerifyPeer）。支持配置自定义 CA 证书（内存 PEM 或文件路径）、客户端证书（mTLS）、跳过校验。请求成功返回，错误（如证书校验失败）以 `Result` 返回。

**Why this priority**: HTTPS 是生产环境的必备能力，证书配置（内网 CA/mTLS/指定证书）是真实部署需求。

**Independent Test**: 可以独立验证——发送 HTTPS GET 请求到已知可达的 HTTPS 端点，验证 200 返回；用自签证书服务器验证默认校验失败、注入 CA 或 skip verification 后成功。

**Acceptance Scenarios**:

1. **Given** 网络可达 https://httpbin.org/get，**When** 开发者发送 HTTPS GET 请求，**Then** 返回 200 且 body 含 "url" 字段。

2. **Given** 本地 HTTPS 服务器用自签证书，**When** 开发者以默认配置（kVerifyPeer）发送请求，**Then** 返回 `Error`（`kCertificateVerificationFailed`）。

3. **Given** 同一自签证书服务器，**When** 开发者配置 `SetVerifyMode(kSkipVerification)` 或注入该自签证书为 CA 后发送请求，**Then** 请求成功返回。

4. **Given** 开发者配置客户端证书/私钥（PEM 或文件路径），**When** 请求要求双向 TLS（mTLS）的服务器，**Then** 握手成功。

---

### User Story 3 - 网络配置生效（超时/重定向/指定网卡）(Priority: P3)

开发者配置 `Client::Options`：连接超时、读超时、重定向跟随、**指定网卡**（绑定源 IP/网卡名）等。配置正确映射到请求行为。超时触发返回对应 `Error`。

**Why this priority**: 配置是生产可用性增强；指定网卡（多网卡/多 IP 主机）是实际网络环境需求。

**Independent Test**: 可以独立验证——设置 1s 连接超时后请求不可达地址，验证返回 `kConnectionTimeout`；验证重定向跟随；验证指定网卡/源 IP 后请求源地址正确。

**Acceptance Scenarios**:

1. **Given** 开发者设置 `SetConnectTimeout(1s)`，**When** 请求一个连接被拒/超时的地址，**Then** 返回 `Error(kConnectionTimeout)`。

2. **Given** 服务器返回 302 到 /redirected，**When** 开发者以默认 `follow_redirects=true` 请求，**Then** 最终响应为 200 且 `effective_url()` 指向重定向目标。

3. **Given** 主机有多个网卡/IP，**When** 开发者配置 `SetInterface("eth1")` 或 `SetLocalAddress("192.168.x.x")`，**Then** 出站连接绑定到指定网卡/源 IP（对端观察到匹配的源地址）。

---

### Edge Cases

- 服务器返回畸形响应（非法 header/body）→ 返回 `Error(kMalformedResponse)` 而非崩溃。
- 请求超时（read_timeout）→ 返回 `Error(kReadTimeout)`。
- DNS 解析失败 → 返回 `Error(kDnsResolutionFailed)`。
- 连接被拒绝 → 返回 `Error(kConnectionRefused)`。
- 空 body 响应 → `has_body()==false`，不崩溃。
- 大响应 body（>8MB）→ 触发流式 `stream()` 读取。
- `Client` 关闭后继续 `Send` → 返回 `Error(kInvalidState)`。
- 指定网卡不存在/无权限 → 返回 `Error(kInvalidArgument)` 或对应连接错误。
- CA 证书解析失败（非法 PEM）→ 返回 `Error(kInvalidArgument)`。

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: 库 MUST 提供 `Client` 同步 API，支持 GET/POST/PUT/DELETE/PATCH/HEAD/OPTIONS 方法与通用 `Send(Request)`。
- **FR-002**: 库 MUST 提供 `Request` 值类型 + Builder，支持 method/url/headers/body（含 JsonBody）/timeout 覆盖，构造时校验（非法 URL、CRLF 注入 → `kInvalidArgument`）。
- **FR-003**: 库 MUST 提供 `Response` 值类型，含 status/headers/body/effective_url，支持流式 `stream()`。
- **FR-004**: 库 MUST 实现同步传输引擎，经共享 CURLM + `curl_multi_poll` 阻塞驱动，复用连接池。
- **FR-005**: 库 MUST 将错误映射为 `Error`（`Result` 返回），覆盖 DNS 失败/连接拒绝/连接超时/读超时/TLS 失败/畸形响应等。
- **FR-006**: 库 MUST 支持超时配置（connect/read/write/total）映射为 libcurl 选项。
- **FR-007**: 库 MUST 支持重定向跟随（默认 true，max_redirects=20）。
- **FR-008**: 库 MUST 支持 HTTPS（OpenSSL TLS，默认 kVerifyPeer），经 `Tls` 配置支持：自定义 CA 证书（内存 PEM **或文件路径**）、客户端证书/私钥（mTLS，PEM 或文件路径）、SNI、跳过校验。
- **FR-009**: 库 MUST 支持指定网卡/源地址绑定（`Options::SetInterface`/`SetLocalAddress`），映射为 libcurl 接口绑定选项。
- **FR-010**: 库 MUST 提供 HTTP 集成测试（本地 HTTP 测试服务器），验证 200/404/POST-JSON 场景。
- **FR-011**: 库 MUST 提供 HTTPS 验证（远程端点或本地自签证书服务器），覆盖默认校验失败/注入 CA/skip/mTLS。
- **FR-012**: 库 MUST 保持 `bazel build //...` 与 `bazel test //...` 零 error/warning。
- **FR-013**: 库 MUST 在公共 API 不暴露 libcurl 类型（curl_slist/CURL 等）。
- **FR-014**: 库 MUST 提供指定网卡验证（多网卡主机上验证源地址绑定）。

### Key Entities *(include if feature involves data)*

- **Client**: 同步请求入口，持 Options + 传输引擎（共享 CURLM）。
- **Request**: 不可变请求值（method/url/headers/body/timeout），Builder 构造。
- **Response**: 不可变响应值（status/headers/body/effective_url），支持流式。
- **Options**: 统一配置（超时/重定向/**网卡绑定**/TLS/代理/连接池），`Client::Options` 与 `Tls` 组合。
- **Tls**: TLS 配置（verify_mode/**CA 证书（PEM 或文件）**/**客户端证书 mTLS**/SNI），映射 CURLOPT_SSL_*。
- **Error/Result**: 统一错误表示（ErrorCode + message），经 Result 返回。
- **Method**: HTTP 方法枚举（Get/Post/Put/Delete/Patch/Head/Options）。

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: 开发者能用 ≤10 行代码完成一次 GET 请求并读取响应（含 client 创建）。
- **SC-002**: 本地集成测试覆盖 200/404/POST-JSON 三个核心场景，全部通过。
- **SC-003**: 所有错误场景（DNS/连接拒绝/超时/TLS 校验失败/畸形响应）返回明确 `Error` 码，无崩溃。
- **SC-004**: `bazel test //...` 全部通过（含新增 HTTP 测试）；`bazel build //...` 零 warning。
- **SC-005**: HTTPS 请求（远程端点）成功返回 200；自签证书默认校验失败、注入 CA/skip 后成功；mTLS 握手成功。
- **SC-006**: 指定网卡验证通过（多网卡主机上源地址正确绑定）。

## Assumptions

- 基于 001 提案架构（同步阻塞 API、libcurl 引擎、Result 错误）与 002 工程结构（src/http 占位、third_party 契约）。
- **类命名**：`Client`/`Request`/`Response`/`Options`/`Tls`/`Method`/`Error`/`Result`（简化优雅，公共命名空间 `netlib`，避免 Http 前缀冗余）。
- HTTP/1.1 为 v1 目标；HTTP/2 不在范围。
- OpenSSL 为全平台 TLS 后端（host 实际构建验证；Android 交叉编译留后续）。
- libcurl/OpenSSL 的源码 Bazel 构建（curl_config.h 方案）在实现阶段落地——**若源码构建复杂度超预期，host 平台可先用系统 libcurl/OpenSSL 链接验证功能，third_party 源码构建作为增强**。
- 集成测试用本地轻量 HTTP 服务器（自研测试 fixture 或系统 python3 -m http.server 辅助），不引入额外第三方依赖。
- 指定网卡/源地址绑定映射为 libcurl `CURLOPT_INTERFACE`/`CURLOPT_LOCALPORT` 相关选项；验证依赖多网卡测试环境（单网卡环境验证配置可设、无崩溃，多网卡 CI 验证绑定生效）。
- 公共 API 命名遵循 001 契约（netlib 命名空间、NETLIB_API 导出）。
- 同步 API：请求阻塞调用线程；异步由上层自行包装。