# Implementation Plan: C++ Cross-Platform Network Library

**Branch**: `001-cpp-network-library` | **Date**: 2026-08-26 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `/specs/001-cpp-network-library/spec.md`

## Summary

Design and implement a cross-platform C++ network library supporting HTTP/1.1 (v1) and WebSocket (v2), built on **libcurl** as the protocol engine. libcurl is built with OpenSSL on host platforms and BoringSSL on Android (via Bazel `select()`). The library exposes a fully asynchronous, promise-based axios-inspired API, with scheduling fully delegated to a user-provided executor extended with fd-watching (`WatchFd`).

## Technical Context

**Language/Version**: C++17

**Primary Dependencies**: libcurl (protocol engine, ≥7.86 for WebSocket), OpenSSL (host TLS backend), BoringSSL (Android TLS backend), Google Test (testing), Bazel 6.5 (build), nlohmann_json (optional, for JSON helpers)

**Storage**: N/A — network library, no persistent storage.

**Testing**: Google Test (from graph_runtime conventions), with local HTTP test servers for integration tests.

**Target Platform**: macOS (x86_64, arm64), Linux (x86_64, aarch64), Android (API 24+)

**Project Type**: Library (static library + shared library)

**Performance Goals**: TLS handshake <500ms, 100+ concurrent connections, 1GB stream with <10MB peak memory.

**Constraints**: Cross-platform (macOS, Linux, Android), fully external scheduling (no internal threads/event loop), C++17, Google C++ Style Guide, Bazel 6.5.

**Scale/Scope**: Client-side library; HTTP/1.1 in v1; WebSocket in v2 (via libcurl 7.86+); iOS/Windows out of scope.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

The constitution is a template with no defined principles. No violations to evaluate. Gate: PASS (pre-research).

Post-design re-check: Constitution still a template; no principles defined; no violations introduced by the design. Gate: PASS.

## Project Structure

### Documentation (this feature)

```text
specs/001-cpp-network-library/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output
└── tasks.md             # Phase 2 output
```

### Source Code (repository root)

```text
src/
├── core/                    # Protocol-independent async foundation
│   ├── executor.h           # External executor interface (Submit/Schedule/WatchFd/UnwatchFd)
│   ├── promise.h            # Promise<T> composition primitive
│   └── error.h              # Error types and codes
├── http/                    # libcurl-based HTTP engine wrapper
│   ├── client.h / .cc       # HttpClient (axios-like API)
│   ├── request.h / .cc      # HttpRequest
│   ├── response.h / .cc     # HttpResponse
│   ├── config.h / .cc       # NetworkConfig (mapped to libcurl options)
│   ├── curl_engine.h / .cc  # libcurl multi integration: socket/timer callbacks → Executor
│   └── promise_bridge.h / .cc  # curl completion → Promise resolution
├── websocket/               # (Future, libcurl 7.86+) WebSocket protocol
│   └── ...
├── tls/                     # TLS config mapping (libcurl SSL backend selected at build)
│   ├── tls_config.h         # TlsConfig → CURLOPT_SSL_* mapping
│   ├── openssl/             # (build-only) libcurl built with OpenSSL on host
│   └── boringssl/           # (build-only) libcurl built with BoringSSL on Android
├── public/                  # Public API surface
│   └── include/netlib/
│       ├── netlib.h         # Umbrella header
│       ├── netlib_export.h  # Export macro
│       ├── http_client.h
│       ├── http_request.h
│       ├── http_response.h
│       ├── network_config.h
│       ├── executor.h
│       └── error.h
├── examples/                # Example applications
│   └── ...
└── tests/                   # Unit and integration tests
    ├── http/
    ├── tls/
    ├── core/
    └── integration/
```

**Structure Decision**: Single library project with a thin `core/` async foundation and an `http/` wrapper around libcurl. The heavy lifting (HTTP parsing, connection pooling, redirects, proxies, TLS handshake) is delegated to libcurl. `core/` provides protocol-independent `Promise`/`Executor`/`Error`. This follows the graph_runtime convention of separating internal implementation from the public headers under `public/include/netlib/`.

## Complexity Tracking

N/A — Constitution check has no violations. No complexity justification required.