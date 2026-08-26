# Research: C++ Cross-Platform Network Library

**Branch**: `001-cpp-network-library` | **Date**: 2026-08-26

## Decision 1: Async I/O Model — External Executor, Promise-Based

- **Decision**: The library provides a fully asynchronous (promise-based) API. No internal threads or event loop. All execution/scheduling is delegated to a user-provided executor interface.
- **Rationale**: Aligns with the user clarification that threading is controlled externally. Keeps the library small and protocol-focused. Matches axios's promise model (`Promise<T>` with `then`/`catch`-style composition).
- **Alternatives considered**:
  - Thread-per-connection: rejected — poor scalability, conflicts with external-executor requirement.
  - Internal event loop (epoll/kqueue): rejected — duplicates user infrastructure, violates "external scheduling" constraint.
  - `std::future`/`std::async`: rejected — `std::future` cannot compose callbacks and requires internal thread management.

## Decision 2: HTTP Engine — libcurl (user-confirmed)

- **Decision**: Use libcurl as the HTTP protocol engine. The library becomes a thin promise-based wrapper over libcurl's multi interface (`curl_multi_*`).
- **Rationale**: libcurl is battle-tested and natively provides HTTP/1.1, HTTP/2, WebSocket (7.86+), connection pooling, redirects, proxies, chunked/streaming, and timeouts. Dramatically reduces implementation risk versus writing an HTTP/1.1 stack from scratch. WebSocket future-phase support comes nearly for free.
- **Implications**:
  - No custom HTTP parser or connection pool code in this repo.
  - Protocol independence (FR-021) holds at a different level: the `core/` module (Promise/Executor/Error) is protocol-agnostic; libcurl provides the multi-protocol transport.
  - libcurl's multi interface is event-driven (`CURLMOPT_SOCKETFUNCTION` + `CURLMOPT_TIMERFUNCTION`), which maps cleanly onto the external-executor model.
- **Alternatives considered**:
  - From-scratch HTTP/1.1: rejected — high implementation effort, protocol edge-case bugs, and duplicates mature functionality.
  - Lightweight parser (llhttp) + own transport: rejected — still requires connection pooling, redirects, proxies, and streaming by hand; libcurl already solves these.

## Decision 3: Async Event Integration — Extended Executor with WatchFd (user-confirmed)

- **Decision**: Extend the `Executor` interface with fd-watching capabilities: `WatchFd(fd, events, callback)` and `UnwatchFd(fd)`, in addition to `Submit` and `Schedule`. The library registers libcurl sockets/timers with the user's executor and calls `curl_multi_socket_action` on events.
- **Rationale**: Fully external scheduling (no internal event loop or threads in the library). The user's executor/event loop (epoll/kqueue/uv/asio) drives readiness; the library only reacts.
- **How libcurl multi integrates**:
  - `CURLMOPT_SOCKETFUNCTION` → library calls `Executor::WatchFd/UnwatchFd`.
  - `CURLMOPT_TIMERFUNCTION` → library calls `Executor::Schedule(delay, ...)`.
  - On fd event / timer fire → library calls `curl_multi_socket_action(multi, fd, evmask, &running)`.
- **Alternatives considered**:
  - `curl_multi_poll` polling on executor threads: rejected — simpler but not event-driven; worse latency and scalability.
  - Internal event loop: rejected — violates "external scheduling" clarification.

## Decision 4: TLS Strategy — libcurl Build-Time SSL Backend

- **Decision**: TLS is handled by libcurl's SSL backend. libcurl is built with OpenSSL on host platforms and BoringSSL on Android, selected via Bazel `select()`. A thin `TlsConfig` maps to libcurl options (`CURLOPT_SSL_VERIFYPEER`, `CURLOPT_SSL_VERIFYHOST`, `CURLOPT_CAINFO`, `CURLOPT_SSLCERT`).
- **Rationale**: Eliminates a custom runtime `TlsAdapter` C++ interface. libcurl already abstracts SSL backends behind a stable C API, and BoringSSL is a supported libcurl build backend on Android. FR-016 (platform-independent public API) is preserved because libcurl's API is stable across backends.
- **Caveats**: BoringSSL removes some deprecated OpenSSL APIs; pin libcurl+BoringSSL versions that are compatible on Android (e.g., NDK-provided or prebuilt).
- **Alternatives considered**:
  - Custom `TlsAdapter` interface over OpenSSL/BoringSSL directly: rejected — duplicates libcurl's own backend abstraction, adds a large interface to maintain.
  - `#ifdef` TLS branches: rejected — pollutes code (FR-016).

## Decision 4: Bazel Platform Selection (mirrors graph_runtime)

- **Decision**: Replicate graph_runtime's Bazel conventions: `platforms/` definitions with `config_setting` + `platform` pairs, `.bazelrc` `--config` aliases, `select()` for TLS backend choice, and `graph_runtime_deps.bzl`-style dependency bootstrap macro.
- **Rationale**: The user explicitly referenced graph_runtime for engineering and cross-platform adaptation. Its patterns are proven: `config_setting_and_platform` macro in `platforms/platforms.bzl`, platform aliases (`build:macos_arm64 --platforms=//platforms:macos_arm64`), and visibility-controlled packages.
- **Key patterns to reuse**:
  - `platforms/platforms.bzl`: `config_setting_and_platform(name, constraint_values)` macro.
  - `platforms/BUILD`: macos_arm64, macos_x86_64, linux_x86_64, linux_aarch64 (+ android_arm64).
  - `.bazelrc`: `--enable_platform_specific_config`, `--features=visibility=hidden`.
  - Dependency bootstrap: idempotent `native.existing_rule()` guards.
  - Export macro pattern (`GRAPH_RUNTIME_API` equivalent) for shared library symbol visibility.
- **Alternatives considered**: Single-set Bazel config without platform abstraction — rejected; cannot conditionally select TLS backends.

## Decision 5: API Design — axios-Inspired Surface

- **Decision**: Public API mirrors axios ergonomics adapted to C++: `HttpClient::Get(url)` / `Post(url, body)` returning `Promise<HttpResponse>`, with configuration via a fluent builder.
- **Rationale**: User clarified the reference should be axios (simpler, promise-based) rather than OkHttp. The promise model maps naturally to the external-executor design.
- **Key design points**:
  - `Promise<T>` supports `Then(callback)`, `Catch(callback)`, `Finally`-style composition.
  - `HttpClient` is configured once (timeouts, TLS, proxy, retry) and reused.
  - Request/response objects are immutable and value-oriented (like axios config/response).
- **Alternatives considered**: OkHttp-style builder/chain — rejected per user clarification. Callback-per-request only — rejected; promise composition is cleaner for retry/redirect chains.

## Decision 6: DNS Resolution

- **Decision**: DNS is delegated to libcurl. libcurl's default thread-safe system resolver (`getaddrinfo`) is used; async-capable resolver (c-ares) is optional and deferred.
- **Rationale**: No separate DNS module needed — libcurl handles resolution internally, including timeouts and retries. Simplifies the architecture.
- **Alternatives considered**: c-ares — deferred (adds a dependency; not needed for v1). Custom resolver module — rejected as redundant.

## Decision 7: No Built-In Observability

- **Decision**: No logging, metrics, or tracing in the library. Errors surface via `Error` codes and promise rejection.
- **Rationale**: User selected minimal observability. Keeps the library lean and dependency-free.

## Decision 8: Promise Composition Primitive

- **Decision**: Implement a lightweight custom `Promise<T>` type (header-only, in `core/`) supporting `Then`/`Catch`/`Finally` composition, driven by the user executor.
- **Rationale**: `std::future` cannot compose continuations; external libraries (Folly/stlab) are heavyweight dependencies. A small custom promise matches the axios mental model and the external-executor requirement.
- **Alternatives considered**: Folly Futures — rejected (heavy dep). stlab — rejected (extra dep). cppcoro — rejected (coroutines not required for v1).

## Open Questions for Implementation Phase (deferred, not blocking)

- Exact libcurl version to pin (recommend latest stable ≥7.86 for WebSocket support).
- Exact OpenSSL (3.x LTS) and BoringSSL versions to pin.
- Whether BoringSSL libcurl is available as a Bazel http_archive or needs NDK integration.
- Whether `nlohmann_json` is needed for v1 (only if JSON request/response helpers are required).
