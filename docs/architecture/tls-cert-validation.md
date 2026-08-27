# TLS Certificate Validation Flow Design

**Branch**: `003-http-implementation` | **Date**: 2026-08-26（已实现，自 001 设计稿同步）

**对应需求**: FR-019/FR-008（自定义证书校验）、SC-003/SC-005（TLS 失败优雅处理、HTTPS 验证）

**实现位置**: `src/public/include/http/tls.h`、`src/tls/tls.cc`（校验）、`src/http/detail/curl_mapping.cc`（映射）、`src/tests/https_test.cc`

**相关设计**: [tls-config.md](tls-config.md)、[core-error.md](core-error.md)、[http-transfer-lifecycle.md](http-transfer-lifecycle.md)

## Overview

定义证书校验的完整流程：从 `Tls` 配置到 libcurl 执行握手、校验失败时的错误映射。配置合法性由 `Tls::Validate()` 前置拒绝；证书链/主机名校验完全委托 libcurl（经其 TLS 后端），库负责选项映射与错误语义化。

## 校验模式

### 模式 A：kVerifyPeer（默认，安全）

```text
用户配置 Tls{verify_mode=kVerifyPeer}
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
用户配置 Tls{verify_mode=kSkipVerification}
  → 映射 CURLOPT_SSL_VERIFYPEER=0, CURLOPT_SSL_VERIFYHOST=0
  → libcurl 不校验证书链、不校验主机名（仅完成加密握手）
  → 仅限测试环境；文档明确安全警告
```

### 模式 C：自定义 CA（内部 CA/私有 PKI）

```text
用户配置 Tls.SetCaFile(path) 或 SetCaPem(pem)
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

## 测试/验收场景对照（spec US2，src/tests/https_test.cc 全部通过）

| 场景 | 配置 | 期望 | 用例 |
|------|------|------|------|
| 自签证书 + 默认配置 | `kVerifyPeer` | `kCertificateVerificationFailed` 拒绝 | `SelfSignedRejectedByDefault` |
| 自签证书 + skip | `SetVerifyMode(kSkipVerification)` | 握手成功 | `SelfSignedAcceptedWhenSkipVerification` |
| 自签证书 + CA 文件注入 | `SetCaFile(...)` | 信任锚 = 该 CA，200 | `SelfSignedAcceptedWhenCaFileInjected` |
| 自签证书 + 内存 CA PEM 注入 | `SetCaPem(pem)` | 同上 | `SelfSignedAcceptedWhenCaPemInjected` |
| mTLS（文件路径） | `SetCertificate(path, path)` | 200 | `ClientCertificateRequiredForMtls` |
| mTLS（内存 PEM） | blob → 临时文件回退 | 200 | `MtlsAcceptedWithInMemoryPem` |
| 非法配置 | 非法 PEM / CA 冲突 / SNI CRLF / 形态混用 | `kInvalidArgument` 前置拒绝 | `TlsValidationTest.*` |

补充说明：自签服务器证书本身也可直接作为 CA 注入（OpenSSL 视为信任锚）；测试采用其签发自签 CA 的更规范路径。

## 边界与约束

- 不做证书固定（certificate pinning，YAGNI，留 polish）。
- 不做 OCSP/CRL 检查（libcurl 默认不做；若需要由上层协议实现）。
- 不做证书内容提取/导出（`CURLINFO_CERTINFO` 留 polish）。

## 评审要点

1. 自定义 CA 的"替代而非追加"语义是否有文档与测试覆盖？
2. `kCertificateVerificationFailed` 不细分为过期/主机名/链，该精度缺失是否可接受（v1）？
3. 证书校验失败默认不重试的策略是否正确（安全考虑）？
4. spec US2 场景 3 的三种配置路径是否都有对应设计？
