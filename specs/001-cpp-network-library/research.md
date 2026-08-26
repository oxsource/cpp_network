# Research: C++ Cross-Platform Network Library

**Branch**: `001-cpp-network-library` | **Date**: 2026-08-26

## Decision 1: 同步阻塞 API（库内无异步抽象）(user-confirmed)

- **Decision**: 库对外提供**同步阻塞 API**。`HttpClient::Send(HttpRequest)` 阻塞调用线程，直接返回 `HttpResponse`。库内**不含** Promise、协程、Executor、WatchFd、事件循环等任何异步抽象；事件与流程编排完全由上层/调用方控制。
- **Rationale**: 用户明确"网络库仅提供基础网络请求，事件及流程不在这里面实现"。异步/并发/重试组合是调用方（上层框架）的职责，库保持薄边界。
- **Implications**:
  - 上层可用自己的线程池/协程/事件循环调用本库的阻塞接口实现异步效果。
  - 移除原 `core/` 模块（Promise/Executor/Error 中的异步部分）。
  - `HttpResponse` 同步持有完整 body（或同步流式句柄）。
- **Alternatives considered**:
  - Promise-based（初稿）：rejected — 用户指出 C++ 缺 async/await 语法糖，裸 Promise 链体验差、模板复杂。
  - C++20 协程：rejected — 需升级语言/NDK，且仍是库内实现事件流程，违背"事件外置"。
  - 异步回调：rejected — 仍是库内回调流程，与"仅基础请求"冲突。

## Decision 2: HTTP Engine — libcurl (user-confirmed)

- **Decision**: Use libcurl as the HTTP protocol engine. The library wraps libcurl with a synchronous API.
- **Rationale**: libcurl is battle-tested and natively provides HTTP/1.1, HTTP/2, WebSocket (7.86+), connection pooling, redirects, proxies, chunked/streaming, and timeouts. Dramatically reduces implementation risk versus writing an HTTP/1.1 stack from scratch.
- **Implications**:
  - No custom HTTP parser or connection pool code in this repo.
  - Protocol independence (FR-021) is satisfied by libcurl itself (multi-protocol); the library's own surface stays thin.
  - libcurl's multi interface + synchronous polling (`curl_multi_poll`) enables connection reuse under a synchronous API.
- **Alternatives considered**:
  - From-scratch HTTP/1.1: rejected — high effort, edge-case bugs.
  - Lightweight parser (llhttp) + own transport: rejected — duplicates libcurl's connection pooling/redirects/proxies.

## Decision 3: 同步传输机制 — 内部共享 CURLM + curl_multi_poll

- **Decision**: 库内部维护一个共享的 `CURLM*`（mutex 保护）。每次 `Send` 在调用线程**阻塞**，通过 `curl_multi_poll` 等待该请求完成；多线程并发调用经锁串行化进入 multi，从而**复用连接池**（FR-012）。无回调、无事件循环、无 Executor。
- **Rationale**: 同步 API 下仍要满足连接复用（keep-alive）与并发（上层多线程调库）。`curl_multi_poll` 是 libcurl 官方提供的同步阻塞轮询接口，天然适合"调用线程阻塞等待"。
- **Simpler alternative (curl_easy_perform)**: 每次 `Send` 用 `curl_easy_perform` 最简单，但**不复用连接**（每次新建 TCP/TLS），违反 FR-012/SC-004。共享 CURLM + `curl_multi_poll` 是同步 + 连接复用的正确取舍。
- **Alternatives considered**:
  - `curl_easy_perform` 每请求独立: rejected — 无连接复用。
  - 事件桥接（WatchFd → 外部 Executor）: rejected — 属异步流程，违背"事件外置"。
  - 库内 epoll/kqueue 事件循环: rejected — 库内事件循环，违背用户约束。

## Decision 4: TLS Strategy — 全平台统一 OpenSSL（修订版）

- **Decision**: TLS is handled by libcurl's SSL backend, using **OpenSSL 3.x LTS on ALL platforms** (host macOS/Linux and Android). No build-time backend `select()` is needed. A thin `TlsConfig` maps to libcurl options (`CURLOPT_SSL_VERIFYPEER`, `CURLOPT_SSL_VERIFYHOST`, `CURLOPT_CAINFO`, `CURLOPT_SSLCERT`).
- **Rationale**: Eliminates a custom runtime `TlsAdapter` C++ interface and the platform `select()` branch. FR-016 (platform-independent public API) is preserved because libcurl's API is stable.
- **Revision note (2026-08-26)**: 初稿为"host=OpenSSL / Android=BoringSSL"双后端 select() 方案。用户决策**全平台统一 OpenSSL**：简化架构、规避 BoringSSL 与 Bazel 6.5 兼容问题。`android-boringssl-build.md` 已废弃。
- **Caveats**: Android 上需确认 OpenSSL 3.x 的 NDK 交叉编译与系统 CA 处理。
- **Alternatives considered**:
  - Custom `TlsAdapter` interface over OpenSSL/BoringSSL directly: rejected — duplicates libcurl's own backend abstraction, adds a large interface to maintain.
  - Android BoringSSL（初稿）: rejected — 全平台统一 OpenSSL（用户决策）。

## Decision 5: Bazel Platform Selection (mirrors graph_runtime)

- **Decision**: Replicate graph_runtime's Bazel conventions: `platforms/` definitions with `config_setting` + `platform` pairs, `.bazelrc` `--config` aliases, `select()` for TLS backend choice, and `graph_runtime_deps.bzl`-style dependency bootstrap macro.
- **Rationale**: The user explicitly referenced graph_runtime for engineering and cross-platform adaptation. Its patterns are proven: `config_setting_and_platform` macro in `platforms/platforms.bzl`, platform aliases (`build:macos_arm64 --platforms=//platforms:macos_arm64`), and visibility-controlled packages.
- **Key patterns to reuse**:
  - `platforms/platforms.bzl`: `config_setting_and_platform(name, constraint_values)` macro.
  - `platforms/BUILD`: macos_arm64, macos_x86_64, linux_x86_64, linux_aarch64 (+ android_arm64).
  - `.bazelrc`: `--enable_platform_specific_config`, `--features=visibility=hidden`.
  - Dependency bootstrap: idempotent `native.existing_rule()` guards.
  - Export macro pattern (`GRAPH_RUNTIME_API` equivalent) for shared library symbol visibility.
- **Alternatives considered**: Single-set Bazel config without platform abstraction — rejected; cannot conditionally select TLS backends.

## Decision 6: API Design — axios-Inspired Surface（同步版）

- **Decision**: Public API mirrors axios ergonomics adapted to C++: `HttpClient::Get(url)` / `Post(url, body)` returning `HttpResponse` synchronously, with configuration via a fluent builder.
- **Rationale**: User clarified the reference should be axios (simple, ergonomic) rather than OkHttp. With the sync model, the axios-like ergonomics (simple per-verb methods, single config object, immutable request/response values) are kept while dropping the promise part.
- **Key design points**:
  - Simple per-verb methods (`Get`/`Post`/`Send`) returning `HttpResponse` directly.
  - `HttpClient` is configured once (timeouts, TLS, proxy, retry) and reused across threads.
  - Request/response objects are immutable and value-oriented (like axios config/response).
  - Retry 由上层循环调用实现（库内单次传输）；redirect 由 libcurl 内部处理。
- **Alternatives considered**: Promise-based axios（初稿）— rejected，见 Decision 1。OkHttp-style builder/chain — rejected per user clarification。

## Decision 7: DNS Resolution

- **Decision**: DNS is delegated to libcurl. libcurl's default thread-safe system resolver (`getaddrinfo`) is used; async-capable resolver (c-ares) is optional and deferred.
- **Rationale**: No separate DNS module needed — libcurl handles resolution internally, including timeouts. Simplifies the architecture.
- **Alternatives considered**: c-ares — deferred (adds a dependency; not needed for v1). Custom resolver module — rejected as redundant.

## Decision 8: 无内建 Observability

- **Decision**: 库内无日志/指标/追踪。所有错误经返回值/`Error` 码暴露（同步 API 下经错误码或异常）。
- **Rationale**: 用户选择 minimal observability（此前澄清）。保持库精简、无额外依赖。

## Open Questions for Implementation Phase (deferred, not blocking)

- Exact libcurl version to pin (recommend latest stable ≥7.86 for WebSocket support).
- Exact OpenSSL (3.x LTS) version to pin.
- Android 上 OpenSSL 3.x 的 NDK 交叉编译与系统 CA 处理方案。
- Whether `nlohmann_json` is needed for v1 (only if JSON request/response helpers are required).
