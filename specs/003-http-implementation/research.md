# Research: 设计实现并验证 HTTP

**Branch**: `003-http-implementation` | **Date**: 2026-08-26

## Decision 1: libcurl 获取策略 — 系统库优先（host 验证），third_party 源码构建增强

- **Decision**: host 平台先用**系统 libcurl/OpenSSL** 链接验证功能（`cc_library` 引用系统库），third_party 的源码 Bazel 构建（curl_config.h 方案）作为后续增强。
- **Rationale**: 002 工程中 libcurl/OpenSSL 仅契约占位，源码构建（configure/curl_config.h）复杂且耗时。系统库（macOS 自带 curl/Homebrew OpenSSL）可立即验证 HTTP 功能与测试，避免阻塞核心实现。与 003 spec Assumptions 一致。
- **Alternatives considered**: 源码 http_archive 构建：rejected（当前阶段，复杂度高）。纯自研 HTTP：rejected（001 已定 libcurl）。
- **实现要点**: `src/http/BUILD.bazel` 用 `linkopts` 链接系统 `-lcurl`；OpenSSL 经 `@openssl//:openssl` 占位或系统链接。TLS 功能验证依赖 curl 的 OpenSSL 后端。

## Decision 2: 类命名 — 简化优雅（用户要求）

- **Decision**: `Client`/`Request`/`Response`/`Options`/`Tls`/`Method`/`Error`/`Result`（`netlib` 命名空间），去掉 `Http` 前缀冗余。
- **Rationale**: 库已限定 netlib 网络语境，`Http` 前缀冗余；短名更优雅。001 契约的 `HttpClient` 等名在新实现中采用新名（001 契约文档保留历史）。
- **Alternatives considered**: 保留 HttpClient 命名：rejected（用户明确要求简化）。

## Decision 3: 同步引擎 — 共享 CURLM + curl_multi_poll（沿用 001 sync-engine 设计）

- **Decision**: 复用 001 架构 `sync-engine.md` 设计：共享 `CURLM*` + mutex 串行化 + `curl_multi_poll` 阻塞驱动，连接池复用。
- **Rationale**: 已定案设计，无新决策。

## Decision 4: 错误模型 — Result<Response> / Error（沿用 001 契约）

- **Decision**: 所有 `Client::Send` 返回 `Result<Response>`（或 `Result<Response>`），错误经 `Error`（ErrorCode + message）。映射表沿用 001 `core-error.md`。
- **Rationale**: 已定案，无新决策。

## Decision 5: 指定网卡/源地址 — libcurl CURLOPT_INTERFACE/CURLOPT_LOCALPORT

- **Decision**: `Options::SetInterface(name)` → `CURLOPT_INTERFACE`（支持网卡名 `"eth1"`、IP、`if!eth1`、`host!ip`）；`Options::SetLocalAddress(ip)` → `CURLOPT_INTERFACE`（IP 形式）。`SetLocalPort` → `CURLOPT_LOCALPORT`（可选）。
- **Rationale**: libcurl 原生支持接口绑定，直接映射。多网卡环境验证源地址；单网卡验证配置可设、无崩溃。
- **Alternatives considered**: 自研 socket 绑定：rejected（libcurl 已提供）。

## Decision 6: 证书配置 — CA/客户端证书（PEM 或文件路径）

- **Decision**: `Tls` 支持：
  - `SetCaCertificate(pem)` → `CURLOPT_CAINFO_BLOB`（curl ≥7.77，内存）。
  - `SetCaFile(path)` → `CURLOPT_CAINFO`（文件路径）。
  - `SetClientCertificate(pem/key)` → `CURLOPT_SSLCERT`/`CURLOPT_SSLKEY`（需文件或 blob，取决于 curl 版本）。
  - `SetVerifyMode(kVerifyPeer|kSkipVerification)` → `CURLOPT_SSL_VERIFYPEER`/`CURLOPT_SSL_VERIFYHOST`。
  - `SetSni(hostname)` → `CURLOPT_SNI_HOSTNAME`（curl ≥7.77）。
- **Rationale**: 覆盖内网 CA（CAINFO）、mTLS（SSLCERT/SSLKEY）、skip 三种真实场景（spec FR-008/US2）。
- **注意**: `CURLOPT_CAINFO_BLOB`/`CURLOPT_SNI_HOSTNAME` 需 curl ≥7.77；macOS 系统 curl 版本需确认，低版本回退临时文件方案。

## Decision 7: 集成测试 — 本地 HTTP/HTTPS 测试服务器

- **Decision**: 用 Python 轻量 fixture 做本地 HTTP 服务器（`python3 -m http.server` 或自研 py 脚本）作为测试前置进程；HTTPS 用 `openssl s_server` 或 python `ssl` 模块 + 自签证书。测试经 `bazel test` 的 fixture 脚本启动/关闭。
- **Rationale**: 不引入第三方 C++ 服务器依赖；python3 系统自带。自签证书在测试时用 openssl CLI 生成并随测试打包。
- **Alternatives considered**: 内嵌 C++ 测试服务器：rejected（开发量大）。系统 curl 服务端：无。

## Decision 8: 构建 — 系统库链接 + 既有工作区

- **Decision**: `src/http/BUILD.bazel` 添加 `cc_library` 链接系统 curl（`linkopts = ["-lcurl"]` + 系统头）。保持 `bazel build //...` 零 warning。OpenSSL 系统库链接同理（若 curl 已静态含则省）。
- **Rationale**: 快速验证路径。`third_party` 源码构建增强留后续提案。
- **风险**: 系统 curl 版本差异（macOS 自带 curl 8.x，功能足够；`CAINFO_BLOB`/`SNI` 需 ≥7.77，macOS 自带 8.4+ 满足）。

## Open Questions (deferred, 非阻塞)

- 系统 libcurl 是否携带 OpenSSL 后端（macOS 默认 SecureTransport/OpenSSL 视构建）：实现阶段用 `curl_version_info` 检测，必要时调整 TLS 测试为 skip 或注入 CA。
- `CURLOPT_CAINFO_BLOB` 在目标系统 curl 版本可用性：实现阶段检测，低版本回退临时 PEM 文件。
