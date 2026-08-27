# Tls 配置设计（已实现）

**Branch**: `003-http-implementation` | **Date**: 2026-08-26
**取代**: `001-cpp-network-library` 阶段的 `TlsConfig` 设计稿

## 版本升级日志（审计锚点，specs/005 FR-002/FR-004）

| 字段 | 值 |
|------|----|
| 当前版本 | OpenSSL 3.0.13 + curl 8.7.1（全平台统一源码构建，pin 见 `cpp_network_deps.bzl`） |
| 上一版本 | 无（2026-08-27 完成统一接入的首次基线） |
| 变更日期 | 2026-08-27 |

> 变更规则：版本变更 **只允许** 发生在 `cpp_network_deps.bzl`；升级后执行
> `make verify` 与 `make android_build` 完成最小回归演练（步骤见 specs/005 quickstart）。

**对应需求**: FR-008（HTTPS + 证书配置）、FR-016/FR-013（不暴露 libcurl/OpenSSL 类型）

**相关设计**: [tls-backend-selection.md](tls-backend-selection.md)、[tls-cert-validation.md](tls-cert-validation.md)、[http-config-mapping.md](http-config-mapping.md)

## Overview

TLS 配置类型为 `cpp_network::comm::Tls`，随 `Options` 注入 `Client`。它只通过 libcurl 的稳定 C API（`CURLOPT_SSL_*`）生效，不暴露 OpenSSL/libcurl 的任何类型或符号。校验逻辑独立在 `//src/tls` 包（`src/tls/tls.cc`），CURLOT 映射由 HTTP 传输引擎逐请求应用。

> 相比 001 设计稿的落地差异：类名简化为 `Tls`；采用 **不可变对象 + `Tls::Builder` 链式构建**（实例创建后无 setter）；CA 支持 **内存 PEM 与文件路径双形态**；校验入口为 `Tls::Validate()`，经 `Options::Validate()` 在 `Client::Create()` 前置拒绝（返回 `Result` 错误而非异常）。

## 实际类型定义（src/public/include/comm/tls.h）

```cpp
namespace cpp_network {
namespace http {

enum class VerifyMode {
  kVerifyPeer,         // Default: verify peer certificate + hostname (secure default)
  kSkipVerification,   // Skip certificate and hostname verification (testing/self-signed only)
};

class Tls {
 public:
  Tls() = default;     // Defaults to kVerifyPeer; immutable after creation

  Result<void> Validate() const;                              // See below
  // Read-only accessors: verify_mode() / ca_pem() / ca_file() /
  //                      client_key() / sni()

  class Builder {    // Chained builder for immutable Tls
   public:
    Builder& SetVerifyMode(VerifyMode mode);
    Builder& SetCaPem(const std::string& pem);                  // In-memory PEM
    Builder& SetCaFile(const std::string& path);                // File path
    Builder& SetCertificate(const std::string& cert,            // mTLS: PEM or file path
                            const std::string& key);
    Builder& SetSni(const std::string& hostname);
    Tls Build() const;
  };
};

}  // namespace http
}  // namespace cpp_network
```

## 校验规则（src/tls/tls.cc，经 Options::Validate() 接入）

`Client::Create()` 时前置校验，失败返回 `Error(kInvalidArgument)`：

1. **CA 来源互斥**：`ca_file` 与 `ca_pem` 不得同时设置。
2. **内联 CA PEM 合法性**：`SetCaPem` 内容必须含完整 PEM 块（`-----BEGIN` 且有匹配 `-----END`，判定用 `Tls::IsPemText`）；文件路径非空。
3. **mTLS 成对**：客户端证书与私钥必须同时设置或同时为空。
4. **mTLS 形态一致**：证书与私钥必须同为内联 PEM 或同为文件路径。
5. **PEM 合法性**：内联证书/私钥须含匹配的 BEGIN/END 块。
6. **SNI 合法**：非空且不含 `\r`/`\n`（防头注入）。

## 映射到 libcurl（src/http/detail/curl_mapping.cc）

| Tls 字段 | libcurl 选项 |
|----------------|--------------|
| `verify_mode == kVerifyPeer` | `CURLOPT_SSL_VERIFYPEER = 1`，`CURLOPT_SSL_VERIFYHOST = 2` |
| `verify_mode == kSkipVerification` | `CURLOPT_SSL_VERIFYPEER = 0`，`CURLOPT_SSL_VERIFYHOST = 0` |
| `ca_file` 非空 | `CURLOPT_CAINFO`（文件路径） |
| `ca_pem`（内存 PEM） | `CURLOPT_CAINFO_BLOB`（后端锁定 curl ≥7.77，直达） |
| 客户端证书/私钥为内联 PEM | `CURLOPT_SSLCERT_BLOB`/`SSLKEY_BLOB`（后端锁定 curl ≥7.71，直达） |
| 客户端证书/私钥为文件路径 | `CURLOPT_SSLCERT` / `CURLOPT_SSLKEY` |
| `sni` | `CURLOPT_SNI_HOSTNAME`（编译期 `#ifdef` 保护） |

### 内联 PEM 直达语义（specs/005 收口）

内联 PEM 的判定由 `Tls::IsPemText` 提供；映射层对 `*_BLOB` 选项**直达、无回退**：setopt 失败即 fail-fast 返回明确错误，不再落临时文件。这一收敛的依据：

1. 全平台唯一后端为源码锁定 curl 8.7.1，BLOB 能力在构建期即成立（单一事实源 `cpp_network_deps.bzl` 钉版本，升级演练保证未来 bump 不低于 7.71/7.77 阈值）；
2. 历史上的"blob 失败→临时文件"两级策略为 macOS **系统** libcurl 运行时怪癖而生（specs/004），specs/005 移除系统传输层后该路径永不触发，实测零临时文件（research.md D3 补记）；
3. 直达语义同时消除了"私钥以明文落盘"的安全降级路径。`Tls::CachedPemPath` 接口已随之从公共 API 移除（需要时可自 git 历史找回）。

## 平台差异收敛

**specs/005 修订（2026-08-27）：全平台统一源码构建后，后端与信任锚语义完全一致。** 差异仅剩"系统锚文件的位置"这一应用侧事实：

| 平台 | 后端 | 系统锚来源（应用注入建议） | 验证等级 |
|------|------|---------------------------|----------|
| macOS | 源码构建 OpenSSL+curl（静态） | `/etc/ssl/cert.pem` | runtime-verified |
| Linux | 同上 | `/etc/ssl/certs/ca-certificates.crt` | build-only（executor pending） |
| Android | 同上 | 无 libcurl 可消费形态——合并 `/system/etc/security/cacerts/*` 为 bundle（工具链已自动化） | runtime-verified |

> BLOB 怪癖与平台特例分支随之消失；"默认拒自签 / 注入通过"在所有平台逐项一致（specs/005 US1 取证）。

## 默认值策略

- `verify_mode` 默认 `kVerifyPeer`（安全默认）。
- 未设置任何 CA 时使用系统信任库。
- 无 mTLS、无 SNI 覆盖默认。

## 边界与约束

- v1 仅支持 PEM（内存或文件路径）；DER 不支持。
- 不提供"跳过主机名校验但保留证书校验"的中间档（二态）。
- 内联 PEM 校验为格式级检查（BEGIN/END 块存在），不做深度解析——非法内容由 TLS 握手期报 `kTlsHandshakeFailed`/`kCertificateVerificationFailed` 兜底。

## 已验证行为（src/tests/https_test.cc）

| 用例 | 场景 | 结果 |
|------|------|------|
| `SelfSignedRejectedByDefault` | 自签服务器，默认配置 | `kCertificateVerificationFailed` |
| `SelfSignedAcceptedWhenSkipVerification` | skip 模式 | 200 |
| `SelfSignedAcceptedWhenCaFileInjected` | 注入 CA 文件 | 200 |
| `SelfSignedAcceptedWhenCaPemInjected` | 注入内存 CA PEM | 200 |
| `ClientCertificateRequiredForMtls` | 文件路径 mTLS | 200 |
| `MtlsAcceptedWithInMemoryPem` | 内存 PEM mTLS | 200 |
| `TlsValidationTest.*` | 非法 CA PEM / CA 来源冲突 / SNI CRLF / PEM 与路径混用 | `kInvalidArgument` |
