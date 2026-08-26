# Core Error Taxonomy Design

**Branch**: `001-cpp-network-library` | **Date**: 2026-08-26

**对应需求**: FR-013（清晰的错误信息）、SC-008（所有网络故障场景优雅失败）

**相关设计**: [core-promise.md](core-promise.md)、[contracts/public-api.md](../../specs/001-cpp-network-library/contracts/public-api.md)

## Overview

定义统一的错误类型 `Error` 与错误码枚举 `ErrorCode`。所有失败（网络、TLS、超时、协议、取消）都以 `Result<T>` 的错误形式暴露（同步 API，见 contracts/public-api.md），绝不产生崩溃或未定义行为。

## 错误码枚举

```cpp
// src/core/error.h
#pragma once

#include <string>

namespace netlib {

enum class ErrorCode {
  kNone = 0,

  // 参数与用法错误
  kInvalidArgument,          // URL 非法、配置非法、必填项缺失
  kInvalidState,             // 在非法状态下调用（如 client 已关闭）

  // 解析/协议错误
  kProtocolError,            // 一般协议错误
  kMalformedResponse,        // 响应格式非法（header/body 解析失败）
  kUnsupportedProtocol,      // 不支持的 scheme（如 ftp://）

  // 连接与网络错误
  kDnsResolutionFailed,      // DNS 解析失败（libcurl CURLE_COULDNT_RESOLVE_*）
  kConnectionRefused,        // 连接被拒绝（CURLE_COULDNT_CONNECT）
  kConnectionClosed,         // 连接被对端关闭
  kConnectionTimeout,        // 连接阶段超时
  kReadTimeout,              // 读超时（CURLE_OPERATION_TIMEDOUT 读方向）
  kWriteTimeout,             // 写超时
  kTotalTimeout,             // 总超时（total_timeout）

  // TLS 错误
  kTlsHandshakeFailed,       // TLS 握手失败（CURLE_SSL_CONNECT_ERROR）
  kCertificateVerificationFailed,  // 证书校验失败（CURLE_PEER_FAILED_VERIFICATION）

  // 资源与并发
  kConnectionPoolExhausted,  // 连接池耗尽（CURLE_TOO_MANY_REDIRECTS 之外，见下）
  kTooManyRedirects,         // 重定向次数超限（CURLE_TOO_MANY_REDIRECTS）
  kOutOfMemory,              // 内存不足
  kCancelled,                // 操作被取消

  // 内部
  kInternalError,            // 引擎内部错误（含回调抛出的异常）
};

// 字符串辅助：返回人类可读错误码名
const char* ErrorCodeToString(ErrorCode code);

// 将 libcurl CURLcode / CURLMcode 映射为 ErrorCode（仅供 src/http 内部使用，
// 不暴露在公共 API）。
ErrorCode MapCurlError(int curl_code, ErrorCode fallback);

class Error {
 public:
  Error() : code_(ErrorCode::kNone) {}
  Error(ErrorCode code, std::string message)
      : code_(code), message_(std::move(message)) {}

  ErrorCode code() const { return code_; }
  const std::string& message() const { return message_; }
  bool ok() const { return code_ == ErrorCode::kNone; }

 private:
  ErrorCode code_;
  std::string message_;
};

inline Error MakeError(ErrorCode code, std::string message) {
  return Error(code, std::move(message));
}

}  // namespace netlib
```

## 错误信息原则

1. **可行动**：message 必须包含失败主体（主机、URL 路径、阶段），如 `"connection to host:port timed out after 5000ms"`。
2. **不泄漏内部细节**：不暴露原始指针/内存地址；libcurl 错误原文保留在 message 中便于排查。
3. **稳定契约**：`ErrorCode` 是稳定枚举（禁止重排），message 文本不保证跨版本稳定（仅用于日志）。

## libcurl → ErrorCode 映射（内部）

| libcurl CURLcode | ErrorCode |
|------------------|-----------|
| `CURLE_OK` | `kNone` |
| `CURLE_COULDNT_RESOLVE_HOST` / `_PROXY` | `kDnsResolutionFailed` |
| `CURLE_COULDNT_CONNECT` | `kConnectionRefused` |
| `CURLE_OPERATION_TIMEDOUT` | 视阶段映射 `kConnectionTimeout`/`kReadTimeout`/`kWriteTimeout`/`kTotalTimeout` |
| `CURLE_SSL_CONNECT_ERROR` | `kTlsHandshakeFailed` |
| `CURLE_PEER_FAILED_VERIFICATION` | `kCertificateVerificationFailed` |
| `CURLE_TOO_MANY_REDIRECTS` | `kTooManyRedirects` |
| `CURLE_UNSUPPORTED_PROTOCOL` | `kUnsupportedProtocol` |
| `CURLE_OUT_OF_MEMORY` | `kOutOfMemory` |
| 其他 | `kProtocolError`（message 保留 `curl_easy_strerror`） |

阶段判定由引擎在记录错误时附加（读/写/连接/总超时由配置与发生位置决定）。

## 边界与约束

- 错误码枚举值稳定，新增只能 append。
- 不定义"错误分类"以外的继承体系（避免异常/错误多态复杂度）。
- `MapCurlError` 属于内部实现，不放公共头文件。

## 评审要点

1. 错误码是否覆盖 spec Edge Cases（DNS、超时、TLS、畸形响应、连接池、重定向）？
2. 错误码是否全部映射自明确语义，无歧义"杂项"归类？
3. message 是否满足"可行动"原则（含主体与阶段）？
