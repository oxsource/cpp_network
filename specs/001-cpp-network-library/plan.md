# Implementation Plan: C++ Cross-Platform Network Library

**Branch**: `001-cpp-network-library` | **Date**: 2026-08-26 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `/specs/001-cpp-network-library/spec.md`

## Summary

设计并实现跨平台 C++ 网络库，基于 **libcurl** 提供**同步阻塞 API**。HTTP/1.1 (v1) + WebSocket (v2)，TLS 统一使用 OpenSSL（全平台 host + Android）。库仅提供基础网络请求（连接/收发/超时/TLS/代理/连接池），**不含任何事件循环、线程调度、Promise/协程等异步流程抽象**——异步与流程编排完全由上层/调用方控制。

## Technical Context

**Language/Version**: C++17

**Primary Dependencies**: libcurl (协议引擎, ≥7.86 for WebSocket), OpenSSL (TLS 后端, 全平台), Google Test (测试), Bazel 6.5 (构建), nlohmann_json (可选, JSON 辅助)

**Storage**: N/A — network library, no persistent storage.

**Testing**: Google Test（graph_runtime 惯例），本地 HTTP 测试服务器做集成测试。

**Target Platform**: macOS (x86_64, arm64), Linux (x86_64, aarch64), Android (API 24+)

**Project Type**: Library (static library + shared library)

**Performance Goals**: TLS 握手 <500ms, 100 并发连接, 1GB 流式 body <10MB 峰值内存。

**Constraints**: 跨平台 (macOS/Linux/Android)、**同步阻塞 API（库内无线程/事件循环/Promise）**、C++17、Google C++ Style Guide、Bazel 6.5。

**Scale/Scope**: 客户端库；v1 HTTP/1.1；v2 WebSocket（libcurl 7.86+）；iOS/Windows out of scope。

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
├── http/                    # libcurl 同步 HTTP 引擎
│   ├── client.h / .cc       # HttpClient（同步 API：Get/Post/.../Send 返回 HttpResponse）
│   ├── request.h / .cc      # HttpRequest
│   ├── response.h / .cc     # HttpResponse
│   ├── config.h / .cc       # NetworkConfig（映射为 libcurl 选项）
│   └── engine.h / .cc       # 同步传输：curl_easy_perform / curl_multi 同步轮询 + 连接池
├── websocket/               # (Future, libcurl 7.86+) WebSocket 同步 API
│   └── ...
├── tls/                     # TLS 配置映射（libcurl SSL 后端构建时选择）
│   ├── tls_config.h         # TlsConfig → CURLOPT_SSL_* 映射
│   ├── openssl/             # (build-only) host
│   └── openssl/           # (build-only) OpenSSL 全平台 TLS 后端
├── public/                  # Public API surface
│   └── include/netlib/
│       ├── netlib.h         # Umbrella header
│       ├── netlib_export.h  # Export macro
│       ├── http_client.h
│       ├── http_request.h
│       ├── http_response.h
│       ├── network_config.h
│       └── error.h
├── examples/                # Example applications
│   └── ...
└── tests/                   # Unit and integration tests
    ├── http/
    ├── tls/
    └── integration/
```

**Structure Decision**: 单库项目，`http/` 封装 libcurl 提供同步请求 API。**无 `core/` 异步层**（无 Promise/Executor/WatchFd）——库边界保持纯粹的基础网络请求。重活（HTTP 解析、连接池、重定向、代理、TLS 握手）由 libcurl 承担；线程/事件/流程由上层自行组织。公共头在 `public/include/netlib/`。

## Complexity Tracking

N/A — Constitution check has no violations. No complexity justification required.