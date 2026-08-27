# Response 值类型（已实现）

**Branch**: `003-http-implementation` | **Date**: 2026-08-26

**对应需求**: FR-006（响应：状态码/headers/body）、FR-007（大 body 流式 — **v1 未实现，见下**）

**实现位置**: `src/public/include/http/response.h`、`src/http/response_stream.cc`、`src/http/engine.cc`

## Overview

`cpp_network::http::Response` 是不可变的入站响应。v1 将整个 body 缓冲在内存；流式读取的公共 API（`Stream`）已就位但为空壳，明确标注 deferred。

## 实际 API

```cpp
class Response {
 public:
  int status() const;                       // ok() is true for 2xx
  const std::string& status_text() const;   // Always "" in v1 (reason phrase not parsed)
  const Headers& headers() const;           // Order-preserving, duplicates allowed (see http-request.md)
  bool has_body() const;
  const std::string& body() const;
  bool ok() const;                          // status ∈ [200, 300)
  const std::string& effective_url() const; // Final URL after redirects
  std::optional<std::string> GetHeader(name) const;   // Delegates to Headers::Get (case-insensitive)
                                            // For multiple values: headers().GetAll(name)
  std::optional<Stream> stream();           // Always returns nullopt in v1 (streaming deferred)
};

class Stream {                              // Empty shell: Impl is an empty struct
  std::int64_t Read(void* out, std::size_t max_bytes, Error* error);
  // In v1 always returns -1 and sets *error = kInvalidState("streaming not supported in buffered mode")
};
```

## 构造路径（engine.cc）

- `status` 来自 `CURLINFO_RESPONSE_CODE`；`effective_url` 来自 `CURLINFO_EFFECTIVE_URL`。
- headers 由 HeaderCallback 逐行解析（trim 首部空白与尾部 CRLF），保序存储。
- **status == 0 的传输视为失败**：返回 `Error(kProtocolError, "missing HTTP status line")`，不会构造 status=0 的 Response。
- `has_body()` = 缓冲区非空。

> 相比 001 设计稿的落地差异：`HttpResponse` → `Response`；`status_code()`/`body_string()` → `status()`/`body()`；`redirected()` 未实现（可用 `effective_url() != req.url()` 判断）；`BodyStream{Read/Skip/Close}` → `Stream{Read}`。

## 流式读取（deferred）

001 设计承诺的"超过阈值切换流式模式 + SC-007（1GB body <10MB 内存）"**未随 spec 003 实现**：

- 引擎 WriteCallback 无条件将全部 body 追加进内存缓冲，无阈值切换。
- `Stream`/`stream()` 是预留 API 形状；启用需在 engine 中延长 easy handle 生命周期至 Stream 析构。
- 后续实现时无需破坏公共 API（字段/方法已就位）。

## 边界与约束

- 大 body 场景 v1 会整段载入内存——使用方需自行评估。
- 不做证书内容提取（CURLINFO_CERTINFO）。
- reason phrase 解析留 polish（HTTP/2 下本就没有 reason phrase）。
