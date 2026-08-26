# HttpRequest Value Type Design

**Branch**: `001-cpp-network-library` | **Date**: 2026-08-26

**对应需求**: FR-005（自定义 headers）、FR-006（请求体 + Content-Type）、FR-001（HTTP 方法）

**用户故事**: US1 (P1) — Send HTTP Request and Receive Response

**相关设计**: [http-client-api.md](http-client-api.md)、[http-response.md](http-response.md)、[contracts/public-api.md](../../specs/001-cpp-network-library/contracts/public-api.md)

## Overview

`HttpRequest` 是不可变值类型，描述一次出站 HTTP 请求。通过 `HttpRequest::Builder` 链式构造，构造后不可变，可并发共享。所有字段在 `Build()` 时校验。

## 类型定义

```cpp
// src/public/include/netlib/http_request.h
#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "netlib/netlib_export.h"

namespace netlib {

enum class HttpMethod {
  kGet, kPost, kPut, kDelete, kPatch, kHead, kOptions,
};

using Headers = std::vector<std::pair<std::string, std::string>>;  // 保序，大小写不敏感合并

class HttpRequest {
 public:
  HttpMethod method() const;
  const std::string& url() const;             // 绝对 URL，未解析
  const Headers& headers() const;
  bool has_body() const;
  const std::string& body() const;            // 仅当 has_body() 时有效
  const std::optional<std::chrono::milliseconds>& timeout() const;  // 请求级覆盖

  // 便捷访问：查找 header（大小写不敏感），返回首个匹配或空 optional。
  std::optional<std::string> GetHeader(const std::string& name) const;

  class Builder {
   public:
    Builder& Method(HttpMethod method);
    Builder& Url(const std::string& url);
    Builder& Header(const std::string& name, const std::string& value);   // 追加
    Builder& Headers(const Headers& headers);                             // 整体覆盖
    Builder& Body(const std::string& body);          // 设置 body，默认 Content-Type: text/plain
    Builder& JsonBody(const std::string& json);      // 设置 body + Content-Type: application/json
    Builder& Timeout(std::chrono::milliseconds ms);  // 请求级超时覆盖

    // 校验并构建；失败返回 Result 错误（kInvalidArgument）。
    Result<HttpRequest> Build() const;

   private:
    HttpMethod method_ = HttpMethod::kGet;
    std::string url_;
    Headers headers_;
    std::string body_;
    bool has_body_ = false;
    std::optional<std::chrono::milliseconds> timeout_;
  };

 private:
  friend class Builder;
  HttpMethod method_;
  std::string url_;
  Headers headers_;
  std::string body_;
  bool has_body_;
  std::optional<std::chrono::milliseconds> timeout_;
};

}  // namespace netlib
```

## 校验规则

`Build()` 必须满足以下校验（任一失败 → `Error(kInvalidArgument, msg)`）：

1. **URL 合法且绝对**：必须含 scheme（`http://` 或 `https://`）。使用 `curl_url()` API 解析校验（避免手写解析）。非法 → 拒绝。
2. **无 CRLF 注入**：header name/value、URL 不得含 `\r` 或 `\n`（`kInvalidArgument`）。
3. **body 与方法约束**：`kGet`/`kHead`/`kOptions` 不允许 body（违反 → `kInvalidArgument`）；`kPost`/`kPut`/`kPatch` 无 body 时发送空 body（Content-Length: 0）。
4. **timeout 非负**：`timeout_ >= 0ms`。

## 语义细节

- **Headers 容器**：用 `vector<pair<name, value>>` 保持顺序（支持重复 header，如多值 Cookie/Set-Cookie 场景）。`GetHeader` 做大小写不敏感查找返回首个匹配。构造时**不合并**同名 header，交给 libcurl 处理（libcurl 对重复 header 按协议合并）。
- **Body 与 Content-Type**：`Body()` 若 headers 中无 Content-Type 则自动补 `Content-Type: text/plain`；`JsonBody()` 自动设置/覆盖为 `application/json`。用户显式 Header 优先级最高（若已设 Content-Type，`Body()` 不覆盖，`JsonBody()` 覆盖）。
- **不可变性**：Builder 构造完成后只读；共享给多个线程并发只读安全（FR-011）。

## 与 libcurl 的衔接（内部）

`http-config-mapping.md` 定义 HttpRequest → CURL 选项：

| HttpRequest 字段 | libcurl 选项 |
|------------------|--------------|
| `method` | `CURLOPT_CUSTOMREQUEST`（GET 可省略）或 `CURLOPT_NOBODY`(HEAD)/`CURLOPT_UPLOAD`(PUT) |
| `url` | `CURLOPT_URL` |
| `headers` | `curl_slist_append` → `CURLOPT_HTTPHEADER` |
| `body` | `CURLOPT_POSTFIELDS` / `CURLOPT_READFUNCTION`（流式见 polish） |
| `timeout` | 覆盖 `CURLOPT_TIMEOUT_MS`（若请求级设置） |

## 边界与约束

- v1 仅字符串 body；二进制 body（`std::string` 可承载但语义上以字节流处理）留 polish。
- 不解析 URL 为结构化对象（保持 `std::string`；解析交给 libcurl `curl_url`）。
- 不在公共 API 暴露 `curl_slist` 等 curl 类型。

## 评审要点

1. 校验规则是否覆盖 URL 非法、CRLF 注入、方法-body 约束？
2. Header 保序 + 大小写不敏感查找的行为是否与 libcurl 合并语义一致？
3. `Body()` vs `JsonBody()` 的 Content-Type 覆盖优先级是否符合直觉？
4. 不可变性是否保证并发只读安全（FR-011）？
