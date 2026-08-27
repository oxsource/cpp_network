# ADR-003: TLS 后端 — 跟随平台，验收标准为 HTTPS + 自定义证书能力

**Status**: Accepted（2026-08-26 第二次修订：由"全平台统一 OpenSSL 源码构建"改为"后端跟随平台"）
**Date**: 2026-08-26
**决策依据文档**: [research.md](../../../specs/001-cpp-network-library/research.md) Decision 4（修订）

## Context

需求 FR-003：不同平台适配不同 TLS；FR-016：公共 API 平台无关、不暴露后端细节。需决定 TLS 抽象方式。

**修订历史**：
1. 初稿：构建时 select()（host=OpenSSL / Android=BoringSSL）。
2. 第一次修订（2026-08-26）：用户决策全平台统一 OpenSSL（规避 BoringSSL 与 Bazel 6.5 兼容问题）。
3. **第二次修订（2026-08-26，本版）**：用户澄清真实需求——**不要求各平台都使用 OpenSSL，只要经 libcurl 支持 HTTPS 及自定义证书即可（特别是 Android）**。据此放弃"源码构建 OpenSSL + 以其编译 libcurl"的落地路径。

关键事实（spec 003 实现期核实）：
- libcurl 不自带任何 TLS 后端，但绑定二进制发行版自带的任一后端即可支持 HTTPS。
- host macOS 系统 libcurl 绑 SecureTransport、Linux 发行版普遍绑 OpenSSL——**系统 libcurl 已满足 HTTPS + 自定义证书能力**，macOS 已实测通过。
- Android 上 NDK 不暴露系统 libssl/libcrypto，未来集成时必须为 libcurl 配一个自带后端；且 libcurl 默认不读 Android 系统 CA store，App 必须注入信任锚。现有 `Tls` API（`SetCaFile`/`SetCaCertificate`/mTLS/skip）已覆盖该场景。

## Decision

TLS 由 libcurl 的 SSL 后端承担，**后端跟随平台惯例，不做统一要求**：

- **host macOS/Linux**：直接链接系统 libcurl（`linkopts = ["-lcurl"]`），使用其后端（macOS 为 SecureTransport，Linux 通常为 OpenSSL）。这是预期状态而非缺陷。
- **Android（延后）**：开发启动时再决定自带后端选型（OpenSSL 3.x / mbedTLS / BoringSSL 等）与构建集成方式（rules_foreign_cc 源码编译或预编译产物），见 tls-backend-selection.md。
- 公共 API 只暴露 `Tls` 配置值类型，映射为 `CURLOPT_SSL_*`；不提供运行时 `TlsAdapter`，不做构建时 select。

**验收标准（取代"统一 OpenSSL"）**：各平台上 HTTPS 请求成功、自定义 CA/客户端证书/skip 校验行为一致（CURLOPT_CAINFO / CAINFO_BLOB / SSLCERT / SSLKEY 语义）。

## Alternatives Considered

| 备选方案 | 被否决原因 |
|----------|-----------|
| 构建时 select()（host=OpenSSL / Android=BoringSSL，初稿） | BoringSSL 与 Bazel 6.5 不兼容；后被"跟随平台"方案整体取代 |
| 全平台统一 OpenSSL 源码构建（第一次修订版） | 用户需求不含"统一 OpenSSL"；源码构建工程量大且在无 Android 真机阶段无法验证，收益不足 |
| 自研 TlsAdapter（多后端手写适配） | 重复 libcurl 已有抽象，接口维护成本高 |
| 运行时后端切换 | 违背"一平台一后端"的简化约束 |

## Consequences

- 平台后端差异对 API 完全透明；公共头无任何 curl/SSL 类型。
- 各平台 TLS 行为由 libcurl 抽象保证一致；后端差异（如 CA store 来源）记录于 tls-config.md 的平台差异表。
- `third_party/openssl` 与 `third_party/libcurl` 维持占位，仅在 Android 集成启动时激活。
- `android-boringssl-build.md` 保持废弃；原 `netlib_select` 平台助手（无 TLS 分支）已随命名统一移除。
- 若未来出现需要锁定特定 OpenSSL 版本的需求（如依赖其特有行为），再重新评估源码构建路径。
