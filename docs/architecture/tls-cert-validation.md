# TLS Certificate Validation Flow Design

**Branch**: `001-cpp-network-library` | **Date**: 2026-08-26

**对应需求**: FR-019（自定义证书校验：跳过校验/自定义 CA）、SC-008（TLS 失败优雅处理）

**用户故事**: US2 (P1) — Platform-Specific TLS Adapter

**相关设计**: [tls-config.md](tls-config.md)、[core-error.md](core-error.md)、[http-transfer-lifecycle.md](http-transfer-lifecycle.md)

## Overview

定义证书校验的完整流程：从 `TlsConfig` 配置到 libcurl 执行握手、校验失败时的错误映射。校验完全委托 libcurl（OpenSSL 后端），库负责选项映射与错误语义化。

## 校验模式

### 模式 A：kVerifyPeer（默认，安全）

```text
用户配置 TlsConfig{verify_mode=kVerifyPeer}
  → 映射 CURLOPT_SSL_VERIFYPEER=1, CURLOPT_SSL_VERIFYHOST=2
  → libcurl 执行握手：
      1) 校验证书链（信任锚 = 系统信任库 或 用户 CA bundle）
      2) 校验主机名匹配（HOST=2 → 同时校验 subject/SAN 与主机名）
  → 成功：继续传输
  → 失败：CURLE_PEER_FAILED_VERIFICATION
       → 引擎映射 ErrorCode::kCertificateVerificationFailed
       → Send 返回 Result<Error>
```

### 模式 B：kSkipVerification（测试/自签）

```text
用户配置 TlsConfig{verify_mode=kSkipVerification}
  → 映射 CURLOPT_SSL_VERIFYPEER=0, CURLOPT_SSL_VERIFYHOST=0
  → libcurl 不校验证书链、不校验主机名（仅完成加密握手）
  → 仅限测试环境；文档明确安全警告
```

### 模式 C：自定义 CA（内部 CA/私有 PKI）

```text
用户配置 TlsConfig{ca_certificates=[PEM...]}
  → 拼接 PEM bundle → CURLOPT_CAINFO（或 CAINFO_BLOB）
  → verify_mode 仍为 kVerifyPeer（默认）
  → 信任锚 = 用户 CA bundle（替代系统信任库，而非追加）
  → 校验流程同模式 A
```

## 决策：信任锚语义

**自定义 CA 是"替代"系统信任库，而非"追加"**。这是 libcurl `CURLOPT_CAINFO` 的固有语义（指定后不再用系统信任库）。

- 若要"追加"，用户需自行拼接系统 CA + 自定义 CA 为一个 bundle 传入（v1 文档指引，不自动合并）。
- 理由：保持与 libcurl/OpenSSL 行为一致，避免隐藏的安全语义差异。

## 错误映射与失败处理

| 场景 | libcurl CURLcode | ErrorCode | message 示例 |
|------|------------------|-----------|--------------|
| 证书链不可信 | `CURLE_PEER_FAILED_VERIFICATION` | `kCertificateVerificationFailed` | `"peer certificate not trusted (host: example.com)"` |
| 主机名不匹配 | `CURLE_PEER_FAILED_VERIFICATION` | `kCertificateVerificationFailed` | `"certificate hostname mismatch (expected: example.com)"` |
| 证书已过期 | `CURLE_PEER_FAILED_VERIFICATION` | `kCertificateVerificationFailed` | `"certificate expired (notAfter: ...)"` |
| 握手失败（协议/密码套件） | `CURLE_SSL_CONNECT_ERROR` | `kTlsHandshakeFailed` | `"TLS handshake failed (error: ...)"` |
| 密码套件不支持 | `CURLE_SSL_CIPHER` | `kTlsHandshakeFailed` | — |

**细化**：`CURLE_PEER_FAILED_VERIFICATION` 不区分具体失败原因；若要区分过期/主机名/链，v1 通过 `CURLOPT_VERBOSE` 或 `SSLKEYLOGFILE` 调试，不设专门错误码。文档明示该限制（评审要点 2）。

## 与同步传输生命周期交互

- 校验发生在 **传输阶段**（TLS 握手，见 http-transfer-lifecycle.md）。
- 失败 → 单次传输返回 `Result<Error>`：
  - 库内不重试（重试由上层实现，默认不重试见 retry-policy.md）；
  - 否则 `Send` 返回 `Result<Error(kCertificateVerificationFailed)>`。
- **重试安全**：证书校验失败默认**不重试**（避免对不可信证书的重复握手；由 `retry_condition` 显式开启）。

## 测试/验收场景对照（spec US2 场景 3）

| 场景 | 配置 | 期望 |
|------|------|------|
| 自签证书 + 未配置 skip | 默认 `kVerifyPeer` | `kCertificateVerificationFailed` 拒绝 |
| 自签证书 + 配置 skip | `SetVerifyMode(kSkipVerification)` | 握手成功，传输继续 |
| 自签证书 + 自定义 CA | `AddCaCertificate(自签PEM)` | 握手成功（信任锚 = 该 CA） |

## 边界与约束

- 不做证书固定（certificate pinning，YAGNI，留 polish）。
- 不做 OCSP/CRL 检查（libcurl 默认不做；若需要由上层协议实现）。
- 不做证书内容提取/导出（`CURLINFO_CERTINFO` 留 polish）。

## 评审要点

1. 自定义 CA 的"替代而非追加"语义是否有文档与测试覆盖？
2. `kCertificateVerificationFailed` 不细分为过期/主机名/链，该精度缺失是否可接受（v1）？
3. 证书校验失败默认不重试的策略是否正确（安全考虑）？
4. spec US2 场景 3 的三种配置路径是否都有对应设计？
