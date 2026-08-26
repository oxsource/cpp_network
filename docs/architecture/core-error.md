# 错误码体系（已实现）

**Branch**: `003-http-implementation` | **Date**: 2026-08-26

**对应需求**: FR-005（错误映射为 `Error`，`Result` 返回）、SC-003（错误场景无崩溃）

**实现位置**: `src/public/include/http/error.h`、`src/public/include/http/result.h`、`src/http/error.cc`、`src/http/engine.cc`（MapCurlError）

## Overview

所有公共 API 失败以 `Result<T>` 携带 `Error` 返回，不抛异常。错误码枚举 `cpp_network::http::ErrorCode` 稳定且 append-only。

## 类型定义

```cpp
enum class ErrorCode {
  kNone = 0,
  kInvalidArgument,             // 参数/配置校验失败（Client::Create / Builder::Build 前置拒绝）
  kInvalidState,                // Client 已 Close 后调用 Send 等
  kProtocolError,               // 协议层失败（含缺状态行、读写错误）
  kMalformedResponse,
  kUnsupportedProtocol,
  kDnsResolutionFailed,
  kConnectionRefused,
  kConnectionClosed,
  kConnectionTimeout,
  kReadTimeout,                 // 低速率检测触发（空闲读超时）
  kWriteTimeout,                // write_timeout 作为硬上限兜底时触发
  kTotalTimeout,                // total_timeout / 请求级 timeout 触发
  kTlsHandshakeFailed,
  kCertificateVerificationFailed,
  kTooManyRedirects,
  kOutOfMemory,
  kCancelled,
  kInternalError,
};

const char* ErrorCodeToString(ErrorCode code);   // 返回枚举名字符串；未知值返回 "kUnknown"

class Error {                 // header-only 值类型
  ErrorCode code() const;
  const std::string& message() const;
  bool ok() const;            // code == kNone
};

template <typename T> class Result { /* ok()/value()/error()/TakeValue() */ };
template <> class Result<void>;     // 校验场景用
```

> 相比 001 设计稿的落地差异：删除 `kConnectionPoolExhausted`（连接池委托 libcurl 排队，无耗尽错误路径）；内部 `MapCurlError` 为 engine.cc 匿名命名空间私有函数，签名 `(CURLcode, const std::string&, ErrorCode)`，不是公共 API。

## libcurl CURLcode → ErrorCode 映射

| CURLcode | ErrorCode | 说明 |
|----------|-----------|------|
| `CURLE_COULDNT_RESOLVE_HOST` / `_RESOLVE_PROXY` | `kDnsResolutionFailed` | |
| `CURLE_COULDNT_CONNECT` | `kConnectionRefused` | |
| `CURLE_OPERATION_TIMEDOUT` | 见下方超时判定 | 同一 CURLcode 覆盖多种超时 |
| `CURLE_SSL_CONNECT_ERROR` | `kTlsHandshakeFailed` | |
| `CURLE_PEER_FAILED_VERIFICATION` | `kCertificateVerificationFailed` | 不区分过期/主机名/链 |
| `CURLE_TOO_MANY_REDIRECTS` | `kTooManyRedirects` | |
| `CURLE_UNSUPPORTED_PROTOCOL` | `kUnsupportedProtocol` | |
| `CURLE_OUT_OF_MEMORY` | `kOutOfMemory` | |
| `CURLE_READ_ERROR` / `CURLE_WRITE_ERROR` | `kProtocolError` | |
| 其他 | `kProtocolError` | |

### CURLE_OPERATION_TIMEDOUT 细分

libcurl 不区分超时来源，引擎按**墙钟耗时 vs 配置上限**判定（100ms 容差）：

1. 有效硬超时（请求级 timeout > total_timeout > write_timeout 兜底，见 http-config-mapping.md）到期 → 对应的 `kTotalTimeout` 或 `kWriteTimeout`
2. 否则若 read_timeout 到期（低速率检测窗口）→ `kReadTimeout`
3. 否则 → `kConnectionTimeout`

## 边界与约束

- `kReadTimeout`/`kWriteTimeout`/`kTotalTimeout` 的细分依赖耗时比较，非精确阶段追踪。
- 枚举值稳定：只允许追加，不允许重排或复用已删码。

## 验证

config_test.cc：连接超时（不可达地址）、读超时（慢速服务器）、总超时标签、请求级覆盖均有用例覆盖。
