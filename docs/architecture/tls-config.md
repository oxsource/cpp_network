# Tls 配置设计（已实现）

**Branch**: `003-http-implementation` | **Date**: 2026-08-26
**取代**: `001-cpp-network-library` 阶段的 `TlsConfig` 设计稿

**对应需求**: FR-008（HTTPS + 证书配置）、FR-016/FR-013（不暴露 libcurl/OpenSSL 类型）

**相关设计**: [tls-backend-selection.md](tls-backend-selection.md)、[tls-cert-validation.md](tls-cert-validation.md)、[http-config-mapping.md](http-config-mapping.md)

## Overview

TLS 配置类型为 `cpp_network::http::Tls`，随 `Options` 注入 `Client`。它只通过 libcurl 的稳定 C API（`CURLOPT_SSL_*`）生效，不暴露 OpenSSL/libcurl 的任何类型或符号。校验逻辑独立在 `//src/tls` 包（`src/tls/tls.cc`），CURLOT 映射由 HTTP 传输引擎逐请求应用。

> 相比 001 设计稿的落地差异：类名简化为 `Tls`；Builder 改为链式 setter 直接返回 `Tls&`；CA 支持 **内存 PEM 与文件路径双形态**；校验入口为 `Tls::Validate()`，经 `Options::Validate()` 在 `Client::Create()` 前置拒绝（返回 `Result` 错误而非异常）。

## 实际类型定义（src/public/include/http/tls.h）

```cpp
namespace cpp_network {
namespace http {

enum class VerifyMode {
  kVerifyPeer,         // 默认：校验对端证书 + 主机名（安全默认）
  kSkipVerification,   // 跳过证书与主机名校验（仅测试/自签场景）
};

class Tls {
 public:
  Tls& SetVerifyMode(VerifyMode mode);
  Tls& SetCaCertificate(const std::string& pem);              // 内存 PEM
  Tls& SetCaFile(const std::string& path);                    // 文件路径
  Tls& SetClientCertificate(const std::string& cert,          // mTLS：PEM 或文件路径
                            const std::string& key);
  Tls& SetSni(const std::string& hostname);

  Result<void> Validate() const;                              // 见下
  // 只读访问器：verify_mode() / ca_pem() / ca_file() / client_cert() /
  //            client_key() / sni()
};

}  // namespace http
}  // namespace cpp_network
```

## 校验规则（src/tls/tls.cc，经 Options::Validate() 接入）

`Client::Create()` 时前置校验，失败返回 `Error(kInvalidArgument)`：

1. **CA 来源互斥**：`ca_file` 与 `ca_certificate` 不得同时设置。
2. **内联 CA PEM 合法性**：`SetCaCertificate` 内容必须含完整 PEM 块（`-----BEGIN` 且有匹配 `-----END`）；文件路径非空。
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
| `ca_certificate`（内存 PEM） | `CURLOPT_CAINFO_BLOB`（运行时 curl ≥7.77）；失败回退临时文件 → `CURLOPT_CAINFO` |
| 客户端证书/私钥为内联 PEM | `CURLOPT_SSLCERT_BLOB`/`SSLKEY_BLOB`（运行时 curl ≥7.71）；失败回退临时文件 → `CURLOPT_SSLCERT`/`SSLKEY` |
| 客户端证书/私钥为文件路径 | `CURLOPT_SSLCERT` / `CURLOPT_SSLKEY` |
| `sni` | `CURLOPT_SNI_HOSTNAME`（编译期 `#ifdef` 保护） |

### Blob 运行时回退（MaterializePem）

部分系统 libcurl（如 macOS 系统库）在头文件中声明了 `*_BLOB` 选项，但运行时对未支持的选项返回 `CURLE_FAILED_INIT`。因此映射层采用"先 blob、后临时文件"的两级策略：临时文件按 PEM 内容缓存（进程生命周期内有效，保证路径跨传输可用），写入 `$TMPDIR/netlib_pem_XXXXXX`。

## 平台差异收敛

| 平台 | 默认 CA 行为 | 说明 |
|------|--------------|------|
| macOS | 系统信任库（libcurl 默认） | 无需额外配置；blob 选项需回退（见上） |
| Linux | 系统信任库（`/etc/ssl/certs` 等） | 无需额外配置 |
| Android | 需显式 CA（后续版本明确默认行为与注入方案） | v1 host 构建验证 |

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
