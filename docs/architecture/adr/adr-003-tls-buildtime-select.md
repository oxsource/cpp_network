# ADR-003: TLS 后端 — 全平台统一 OpenSSL

**Status**: Accepted（2026-08-26 修订：由构建时 select() 改为全平台 OpenSSL）
**Date**: 2026-08-26
**决策依据文档**: [research.md](../../../specs/001-cpp-network-library/research.md) Decision 4（修订）

## Context

需求 FR-003：不同平台适配不同 TLS（初稿为 host=OpenSSL / Android=BoringSSL）；FR-016：公共 API 平台无关、不暴露后端细节。需决定 TLS 抽象方式。

**修订背景**：2026-08-26 用户决策**全平台统一使用 OpenSSL**。理由：简化架构（无平台 select 分支）、规避 BoringSSL 与 Bazel 6.5 的兼容问题（BoringSSL master 使用 Bazel 8 属性）。

## Decision

TLS 由 libcurl 的 SSL 后端承担，**全平台（host macOS/Linux 与 Android）统一使用 OpenSSL 3.x LTS**。不提供运行时 `TlsAdapter` C++ 接口；不进行构建时后端 select。公共 API 只暴露 `TlsConfig`（映射为 `CURLOPT_SSL_*`）。

`src/tls/BUILD.bazel` 直接依赖 `@openssl//:openssl` + `@libcurl//:libcurl_openssl`（见 tls-backend-selection.md 修订版）。

## Alternatives Considered

| 备选方案 | 被否决原因 |
|----------|-----------|
| 构建时 select()（host=OpenSSL / Android=BoringSSL，初稿） | 用户决策全平台 OpenSSL；BoringSSL 与 Bazel 6.5 不兼容（Bazel 8 属性 cxxopts） |
| 自研 TlsAdapter（OpenSSL/BoringSSL 双后端） | 重复 libcurl 已有抽象，接口维护成本高 |
| 运行时后端切换 | 违背"一平台一后端"的简化约束 |

## Consequences

- 平台后端差异对 API 完全透明；公共头无任何 curl/SSL 类型。
- Android 需处理系统 CA（libcurl 默认不加载）与 OpenSSL 交叉编译（NDK 工具链），见 tls-config.md / host-openssl-build.md。
- 共享库构建下 SSL 符号经 `-fvisibility=hidden` 隐藏。
- 无需维护 `netlib_select` TLS 分支；`android-boringssl-build.md` 废弃。
