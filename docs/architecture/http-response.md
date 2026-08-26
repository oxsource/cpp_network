# HttpResponse Value Type Design

**Branch**: `001-cpp-network-library` | **Date**: 2026-08-26

**对应需求**: FR-007（流式大 body）、SC-002（全部状态码）、SC-007（1GB body <10MB 内存）

**用户故事**: US1 (P1) — Send HTTP Request and Receive Response

**相关设计**: [http-request.md](http-request.md)、[http-client-api.md](http-client-api.md)、[contracts/public-api.md](../../specs/001-cpp-network-library/contracts/public-api.md)

## Overview

`HttpResponse` 是不可变值类型，表示一次完成的 HTTP 响应：状态码、状态文本、headers、响应体。body 支持**缓冲读取**（小 body）与**流式读取**（大 body，SC-007 要求流 1GB 内存峰值 <10MB）。

## 类型定义

```cpp
// src/public/include/netlib/http_response.h
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "netlib/netlib_export.h"

namespace netlib {

class HttpResponse {
 public:
  int status_code() const;                 // 100~599（含 libcurl 特殊 0/负值归为 kProtocolError）
  const std::string& status_text() const;  // reason phrase（可为空）

  const Headers& headers() const;          // 保序（与 HttpRequest::Headers 同型）
  std::optional<std::string> GetHeader(const std::string& name) const;

  // —— body 访问 ——
  bool has_body() const;
  // 缓冲读取：一次性返回完整 body。适用于已知小 body。
  const std::string& body_string() const;

  // 流式读取：按块读取，避免整体载入内存（SC-007）。
  // 返回 nullopt 表示当前响应已缓冲（无流式句柄）。
  std::optional<BodyStream> body_stream();

  // 元信息
  bool ok() const;              // status_code 2xx
  bool redirected() const;      // 发生了重定向
  const std::string& effective_url() const;  // 最终 URL（重定向后）

 private:
  friend class SyncEngine;      // 仅内部构造
  int status_code_;
  std::string status_text_;
  Headers headers_;
  std::string body_;                       // 缓冲模式
  std::shared_ptr<StreamHandle> stream_;   // 流式模式（见下）
  std::string effective_url_;
};

// 流式句柄（RAII；析构即释放 curl 读上下文）
class BodyStream {
 public:
  // 读取至多 max_bytes 字节到 out；返回实际读取字节数（0 = EOF）。
  // 该调用是同步阻塞的（调用方自行决定在哪个线程调用），适合大文件落盘。
  // 出错返回负值并填充 error。
  std::int64_t Read(void* out, std::size_t max_bytes, Error* error);

  // 跳过（可选，用于大文件 seek）。
  std::int64_t Skip(std::int64_t bytes, Error* error);

  void Close();

 private:
  friend class HttpResponse;
  struct Impl;
  std::shared_ptr<Impl> impl_;
};

}  // namespace netlib
```

## 读取模式选择

### 缓冲模式（默认）

- libcurl 通过 `CURLOPT_WRITEFUNCTION` 将 body 累积到 `std::string`。
- 内存上界：**无硬上限**；但触发策略见下，防止无界内存增长。
- 触发条件：**content-length 已知且 ≤ 阈值（默认 8MB）** 或 body 累计超过阈值时切换为流式。

### 流式模式（SC-007 保障，同步）

- 当响应 content-length > 阈值（默认 8MB）或未知长度且累计超过阈值时，引擎**立即停止缓冲**，保留底层 curl 传输的读上下文（`CURLOPT_WRITEFUNCTION` 改为丢弃/最小缓冲），通过 `BodyStream` 暴露 `Read()`。
- **同步语义**：`Send` 在读到响应头 + 完成阈值判定后返回（body 未读完），`HttpResponse` 携带 `body_stream()` 句柄。之后调用方**同步调用 `Read()`** 拉取数据（阻塞调用线程，同 sync-engine.md 的驱动方式）。相比异步版，无需 Promise resolve 时机约定 —— `Send` 返回即代表"头读完，可流式读取"。
- `Read()` 同步阻塞，返回实际读取字节数（0 = EOF）。
- 内存保证：任意时刻只保留一个块（默认 64KB）在内存，满足 SC-007（1GB 流式 <10MB 峰值）。
- 若用户不消费直接丢弃 `HttpResponse`，流式句柄析构 → 取消剩余传输。

## 同步完成语义

- **缓冲模式**：`Send` 返回时响应（含 body）已完整在 `body_string()`。
- **流式模式**：`Send` 在响应头 + 阈值判定完成后返回（body 未读完）；`body_stream()` 返回句柄，用户同步 `Read()` 消费。

设计权衡：流式模式牺牲"`Send` 返回 = 完整响应"的简单性，换取 SC-007 内存约束。contracts 不变量 #5 已补充此语义。

## 状态码处理

- 正常：`CURLE_OK` 时从 libcurl 读取 `CURLINFO_RESPONSE_CODE`。
- 特殊值：libcurl 可能返回 0（连接成功但无响应，如 `CURLE_*` 失败路径）；此时以 `Error(kProtocolError)` 返回 Result 错误，不构造 0 状态码的 HttpResponse。
- 2xx → `ok()==true`；重定向（3xx）若 `follow_redirects=false` 则返回 3xx 响应本身（不自动跟随）。

## 边界与约束

- v1 缓冲模式上限与流式阈值固定（8MB 默认），后续 polish 可配置化。
- `BodyStream::Read` 为同步阻塞 API；不提供异步 Read。
- 不暴露 libcurl 的 `CURL*` 读上下文到公共 API。

## 评审要点

1. 流式模式下 `Send` 返回时机（头读完）是否有明确文档与调用方约束？
2. SC-007（1GB <10MB）是否通过 64KB 块 + 阈值切换得到保证？
3. 状态码为 0/负值的处理（返回错误而非构造非法 HttpResponse）是否正确？
4. 流式句柄析构时是否取消剩余传输（资源释放）？
