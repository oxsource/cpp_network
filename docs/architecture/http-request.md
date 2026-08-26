# Request 值类型（已实现）

**Branch**: `003-http-implementation` | **Date**: 2026-08-26

**对应需求**: FR-006（请求描述：方法/URL/headers/body/超时）

**实现位置**: `src/public/include/http/request.h`、`src/public/include/http/method.h`、`src/http/request.cc`

## Overview

`cpp_network::http::Request` 是不可变的出站请求描述，经 `Request::Builder` 构建。`Build()` 时做全部校验（前置拒绝，错误码 `kInvalidArgument`），引擎层不再重复校验。

## 实际 API

```cpp
using Headers = std::vector<std::pair<std::string, std::string>>;   // 保序、可重复

enum class Method { kGet, kPost, kPut, kDelete, kPatch, kHead, kOptions };

class Request {
 public:
  Method method() const;
  const std::string& url() const;
  const Headers& headers() const;
  bool has_body() const;
  const std::string& body() const;
  const std::optional<std::chrono::milliseconds>& timeout() const;  // 请求级覆盖
  std::optional<std::string> GetHeader(const name) const;           // 大小写不敏感首匹配

  class Builder {
    Builder& SetMethod(Method);          // 默认 kGet
    Builder& Url(const std::string&);
    Builder& Header(name, value);        // 追加
    Builder& SetHeaders(const Headers&);
    Builder& Body(const std::string&);       // 无 CT 时补 text/plain
    Builder& JsonBody(const std::string&);   // 设置/覆盖 CT 为 application/json
    Builder& Timeout(std::chrono::milliseconds);
    Result<Request> Build();
  };
};
```

> 相比 001 设计稿的落地差异：`HttpMethod` → `Method`；Builder 方法名 `SetMethod`/`SetHeaders`。

## 校验规则（Build()）

1. URL 必须为绝对地址（手写前缀检查 `http://` / `https://`；完整语法解析交给 libcurl，畸形 URL 的错误在传输期报出）。
2. URL 与 header name/value 不得含 CRLF。
3. GET/HEAD/OPTIONS 禁止携带 body。
4. timeout ≥ 0。

## Content-Type 语义

- `Body()`：仅当尚无任何 Content-Type 头时补 `"text/plain"`（大小写不敏感匹配）。
- `JsonBody()`：设置或**覆盖**已有 Content-Type 为 `"application/json"`（大小写不敏感匹配）。
- 显式 `Header("Content-Type", ...)` 声明先于 Body/JsonBody 调用时优先保留（JsonBody 除外，它会覆盖）。

## 映射到 libcurl

| Request 字段 | CURLOPT | 说明 |
|--------------|---------|------|
| url | `CURLOPT_URL` | |
| method (≠HEAD) | `CURLOPT_CUSTOMREQUEST` | |
| method == HEAD | `CURLOPT_NOBODY = 1` | libcurl 原生发 HEAD 并跳过 body 阶段 |
| body | `CURLOPT_POSTFIELDS` + `POSTFIELDSIZE_LARGE` | 仅 has_body 时设置；无 body 不发送 Content-Length: 0 |
| headers | `CURLOPT_HTTPHEADER`（curl_slist） | 保序追加 |
| timeout | `CURLOPT_TIMEOUT_MS`（最高优先级） | 见 http-config-mapping.md |

## 边界与约束

- 无 body 的 POST/PUT/PATCH 不主动发送空 body。
- v1 不支持 multipart/form-data（用 Header + Body 手动构造）。
