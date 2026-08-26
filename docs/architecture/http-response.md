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
  int status() const;                       // 2xx 时 ok() 为 true
  const std::string& status_text() const;   // v1 恒为 ""（不解析 reason phrase）
  const Headers& headers() const;           // 保序，可重复
  bool has_body() const;
  const std::string& body() const;
  bool ok() const;                          // status ∈ [200, 300)
  const std::string& effective_url() const; // 重定向后的最终 URL
  std::optional<std::string> GetHeader(name) const;   // 大小写不敏感首匹配
  std::optional<Stream> stream();           // v1 恒返回 nullopt（streaming deferred）
};

class Stream {                              // 空壳：Impl 为空结构体
  std::int64_t Read(void* out, std::size_t max_bytes, Error* error);
  // v1 恒返回 -1 且 *error = kInvalidState("streaming not supported in buffered mode")
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
