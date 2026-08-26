# ADR-003: TLS 后端构建时选择（Bazel select()）

**Status**: Accepted
**Date**: 2026-08-26
**决策依据文档**: [research.md](../../../specs/001-cpp-network-library/research.md) Decision 4

## Context

需求 FR-003：不同平台适配不同 TLS（host 用 OpenSSL，Android 用 BoringSSL）；FR-016：公共 API 平台无关、不暴露后端细节。需决定 TLS 抽象方式。

## Decision

TLS 由 libcurl 的 SSL 后端承担，**构建时**经 Bazel `select()` 选择：host（macOS/Linux）→ OpenSSL，Android → BoringSSL。不提供运行时 `TlsAdapter` C++ 接口。公共 API 只暴露 `TlsConfig`（映射为 `CURLOPT_SSL_*`）。

`src/tls/BUILD.bazel` 用 `netlib_select` 切换 libcurl 后端依赖（见 tls-backend-selection.md）。

## Alternatives Considered

| 备选方案 | 被否决原因 |
|----------|-----------|
| 自研 TlsAdapter（OpenSSL/BoringSSL 双后端） | 重复 libcurl 已有抽象，接口维护成本高 |
| `#ifdef` 分支 | 污染代码，破坏 FR-016 平台无关性 |
| 运行时后端切换 | 违背"一平台一后端"的简化约束 |

## Consequences

- 平台后端差异对 API 完全透明；公共头无任何 curl/SSL 类型。
- Android 需处理系统 CA（libcurl 默认不加载），见 android-boringssl-build.md。
- 共享库构建下 SSL 符号经 `-fvisibility=hidden` 隐藏。
- 每个平台构建验证矩阵（macOS/Linux/Android）需在 CI 覆盖。
