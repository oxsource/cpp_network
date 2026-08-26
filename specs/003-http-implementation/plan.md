# Implementation Plan: 设计实现并验证 HTTP

**Branch**: `003-http-implementation` | **Date**: 2026-08-26 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `/specs/003-http-implementation/spec.md`

## Summary

在 002 工程结构基础上，实现 001 架构的 HTTP 层：同步 `Client`/`Request`/`Response` 公共 API、同步传输引擎（共享 CURLM + `curl_multi_poll`）、错误映射（`Result`/`Error`）、超时/重定向/指定网卡/证书配置，并用本地 HTTP/HTTPS 测试服务器集成验证。

**类命名**（用户要求简化优雅）：`Client`、`Request`、`Response`、`Options`、`Tls`、`Method`、`Error`、`Result`（命名空间 `netlib`）。

## Technical Context

**Language/Version**: C++17

**Primary Dependencies**: libcurl（协议引擎，同步 multi 接口）、OpenSSL 3.x（TLS，全平台）、Google Test（测试）。

**Storage**: N/A — 无持久存储。

**Testing**: Google Test 集成测试；本地 HTTP/HTTPS 测试服务器 fixture。

**Target Platform**: host macOS (arm64) 实际构建验证；Linux/Android 配置就绪。

**Project Type**: C++ 库（在既有 netlib 库中新增 HTTP 功能）。

**Performance Goals**: 本地请求毫秒级完成；连接池复用（SC-002 隐式）。

**Constraints**: 同步阻塞 API、不暴露 libcurl 类型（FR-013）、零 warning 构建（FR-012）、Google C++ Style。

**Scale/Scope**: v1 HTTP/1.1；不涉及 WebSocket/HTTP2。

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

Constitution 仍为模板，无定义原则，无违规可评估。Gate: PASS。

## Project Structure

### Documentation (this feature)

```text
specs/003-http-implementation/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output
└── tasks.md             # Phase 2 output
```

### Source Code (repository root)

```text
src/http/                        # HTTP 实现（替换占位）
├── BUILD.bazel
├── client.h / client.cc         # netlib::Client（同步 API）
├── request.h / request.cc       # netlib::Request + Builder
├── response.h / response.cc     # netlib::Response（含流式 BodyStream）
├── options.h / options.cc       # netlib::Options（超时/重定向/网卡/连接池）
├── tls.h / tls.cc               # netlib::Tls（证书配置）
├── result.h                     # netlib::Result<T>
├── error.h / error.cc           # netlib::Error + ErrorCode
├── engine.h / engine.cc         # 同步传输引擎（共享 CURLM + curl_multi_poll）
└── detail/
    └── curl_mapping.cc          # Request/Options/Tls → CURLOPT 映射
src/public/include/http/       # 公共 API 头（由 src/http 导出，http_ 前缀）
├── netlib.h                     # umbrella（include client/request/response/options/tls/error/result）
└── netlib_export.h
src/tests/
├── BUILD.bazel
├── smoke_test.cc                # 既有冒烟测试
├── http_integration_test.cc     # 本地 HTTP 服务器集成测试（US1）
├── https_test.cc                # HTTPS/证书验证（US2）
└── config_test.cc               # 配置生效测试（US3）
```

**Structure Decision**: 在 `src/http/` 落地全部实现（002 已建占位目录）。公共头经 `src/public/include/http/` 暴露（命名空间 `cpp_network::http`，文件名 `http_` 前缀），内部实现（engine/curl_mapping）隔离，不泄漏 libcurl 类型（FR-013）。

## Complexity Tracking

N/A — Constitution 无违规，无需复杂度论证。
