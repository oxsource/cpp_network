# TlsConfig Type Design

**Branch**: `001-cpp-network-library` | **Date**: 2026-08-26

**对应需求**: FR-003（平台无关 TLS 抽象）、FR-019（自定义证书校验）、FR-016（不暴露后端细节）

**用户故事**: US2 (P1) — Platform-Specific TLS Adapter

**相关设计**: [tls-backend-selection.md](tls-backend-selection.md)、[tls-cert-validation.md](tls-cert-validation.md)、[core-error.md](core-error.md)、[contracts/public-api.md](../../specs/001-cpp-network-library/contracts/public-api.md)

## Overview

`TlsConfig` 是暴露给用户的唯一 TLS 配置类型。它只通过 libcurl 的稳定 C API（`CURLOPT_SSL_*`）生效，不暴露 OpenSSL 的任何类型或符号，保证 FR-016（平台无关公共 API）。

## 类型定义

```cpp
// src/public/include/netlib/tls_config.h
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "netlib/netlib_export.h"

namespace netlib {

enum class VerifyMode {
  kVerifyPeer,         // 默认：校验对端证书 + 主机名（安全默认）
  kSkipVerification,   // 跳过证书与主机名校验（仅测试/自签场景）
};

class TlsConfig {
 public:
  VerifyMode verify_mode() const;                                    // 默认 kVerifyPeer
  const std::vector<std::string>& ca_certificates() const;           // PEM/DER 列表；空 = 系统信任库
  const std::optional<std::string>& client_certificate() const;      // 客户端证书（PEM，mTLS）
  const std::optional<std::string>& client_private_key() const;      // 客户端私钥（PEM，mTLS）
  const std::optional<std::string>& sni_hostname() const;            // SNI 覆盖（默认取 URL host）

  class Builder {
   public:
    Builder& SetVerifyMode(VerifyMode mode);
    Builder& AddCaCertificate(const std::string& pem_or_der);   // 追加 CA
    Builder& SetClientCertificate(const std::string& pem, const std::string& key);
    Builder& SetSniHostname(const std::string& hostname);
    std::optional<Error> Build(TlsConfig* out) const;           // 校验
    TlsConfig Build() const;                                    // 校验失败抛异常
   private:
    VerifyMode verify_mode_ = VerifyMode::kVerifyPeer;
    std::vector<std::string> ca_certificates_;
    std::optional<std::string> client_certificate_;
    std::optional<std::string> client_private_key_;
    std::optional<std::string> sni_hostname_;
  };

 private:
  VerifyMode verify_mode_ = VerifyMode::kVerifyPeer;
  std::vector<std::string> ca_certificates_;
  std::optional<std::string> client_certificate_;
  std::optional<std::string> client_private_key_;
  std::optional<std::string> sni_hostname_;
};

}  // namespace netlib
```

## 校验规则

`Build()` 校验：

1. **PEM 可解析**：`ca_certificates` / `client_certificate` / `client_private_key` 必须为合法 PEM（`-----BEGIN ...-----` 包裹）。用 libcurl 的 `curl_easy_setopt` 应用时的失败兜底（见下）。
2. **mTLS 成对**：`client_certificate` 与 `client_private_key` 必须同时设置或同时为空，否则 `kInvalidArgument`。
3. **SNI hostname 合法**：非空时不得含 `\r`/`\n`/空白。
4. **verify_mode**：`kSkipVerification` 仅在显式设置时生效（默认安全）。

## 映射到 libcurl（内部）

由 `src/tls/internal/ssl_backend.cc` 实现（见 tls-backend-selection.md）：

| TlsConfig 字段 | libcurl 选项 |
|----------------|--------------|
| `verify_mode == kVerifyPeer` | `CURLOPT_SSL_VERIFYPEER = 1`，`CURLOPT_SSL_VERIFYHOST = 2` |
| `verify_mode == kSkipVerification` | `CURLOPT_SSL_VERIFYPEER = 0`，`CURLOPT_SSL_VERIFYHOST = 0` |
| `ca_certificates` 非空 | 拼接为一个 PEM bundle → `CURLOPT_CAINFO`（临时文件）或 `CURLOPT_CAINFO_BLOB`（curl ≥7.77，内存） |
| `ca_certificates` 空 | 不设置 CAINFO → 使用系统信任库（host）或 Android 系统 CA |
| `client_certificate` / `client_private_key` | `CURLOPT_SSLCERT` / `CURLOPT_SSLKEY` |
| `sni_hostname` | `CURLOPT_SNI_HOSTNAME`（curl ≥7.77）；旧版本用 URL host（忽略该覆盖） |

## 平台差异收敛

| 平台 | 默认 CA 行为 | 说明 |
|------|--------------|------|
| macOS | 系统信任库（`curl` 默认，经 libcurl 的 SecureTransport/OpenSSL 路径） | 无需额外配置 |
| Linux | 系统信任库（`/etc/ssl/certs` 等） | 无需额外配置 |
| Android | **需要显式 CA**：Android 系统 CA 在设备 trust store，libcurl（OpenSSL 后端）默认不带 Android 系统 CA；方案见 `android-boringssl-build.md`（已废弃）或全平台 OpenSSL 的 NDK CA 注入 | v1 默认行为需文档明确 |

## 默认值策略

- `verify_mode` 默认 `kVerifyPeer`（安全默认，contracts 不变量 #9）。
- `ca_certificates` 默认空（系统信任库）。
- 无 mTLS、无 SNI 覆盖默认。

## 边界与约束

- v1 证书为 **PEM 字符串**；DER/文件路径留 polish。
- SNI 覆盖依赖 curl ≥7.77；低版本降级为忽略（文档标注）。
- 不提供"跳过主机名校验但保留证书校验"的中间档（v1 二态）。

## 评审要点

1. 默认 `kVerifyPeer` 是否满足安全默认（contracts #9）？
2. Android 无默认 CA 的行为是否有明确文档与规避指引？
3. mTLS 证书/私钥成对校验是否完整？
4. CA 内存注入（CAINFO_BLOB）与临时文件回退策略是否明确？
