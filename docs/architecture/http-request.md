# Request 值类型（已实现）

**Branch**: `003-http-implementation` | **Date**: 2026-08-26

**对应需求**: FR-006（请求描述：方法/URL/headers/body/超时）

**实现位置**: `src/public/include/http/request.h`、`src/public/include/http/method.h`、`src/http/request.cc`

## Overview

`cpp_network::http::Request` 是不可变的出站请求描述，经 `Request::Builder` 构建。`Build()` 时做全部校验（前置拒绝，错误码 `kInvalidArgument`），引擎层不再重复校验。

## 实际 API

```cpp
// Order-preserving multimap of header fields with duplicates allowed
// (modeled after okhttp3.Headers).
// Case-insensitive lookup; immutable value type, built via Builder:
class Headers {
 public:
  std::optional<std::string> Get(name) const;        // First case-insensitive match
  std::vector<std::string> GetAll(name) const;       // All values for a given name
  bool Has(name) const;
  int size() const;                                  // Number of field lines (incl. duplicates)
  const std::string& name(int i) const;
  const std::string& value(int i) const;
  const std::vector<std::pair<std::string, std::string>>& fields() const;

  class Builder {
    Builder& Add(name, value);     // Appends, keeping duplicates (Set-Cookie style)
    Builder& Set(name, value);     // Replaces all occurrences of the same name (last-wins)
    Builder& Remove(name);         // Removes all occurrences of the same name
    Builder& Clear();
    Headers Build() const;
  };
};

enum class Method { kGet, kPost, kPut, kDelete, kPatch, kHead, kOptions };

class Request {
 public:
  Method method() const;
  const std::string& url() const;
  const Headers& headers() const;
  bool has_body() const;
  const std::string& body() const;
  const std::optional<std::chrono::milliseconds>& timeout() const;  // Per-request override
  std::optional<std::string> GetHeader(const name) const;           // Delegates to Headers::Get

  class Builder {
    Builder& SetMethod(Method);          // Defaults to kGet
    Builder& Url(const std::string&);
    Builder& Header(name, value);        // Appends (Headers::Builder::Add)
    Builder& SetHeaders(const Headers&);
    Builder& Body(const std::string&);       // Fills in text/plain when no CT is set
    Builder& JsonBody(const std::string&);   // Sets/overrides CT to application/json (Set semantics)
    Builder& Timeout(std::chrono::milliseconds);
    Result<Request> Build();
  };
};
```

> 相比 001 设计稿的落地差异：`HttpMethod` → `Method`；Builder 方法名 `SetMethod`/`SetHeaders`；`Headers` 由裸 vector 升级为专用类型（保序可重复 + 大小写不敏感查找，参照 okhttp3.Headers）。

## 校验规则（Build()）

1. URL 必须为绝对地址（手写前缀检查 `http://` / `https://`；完整语法解析交给 libcurl，畸形 URL 的错误在传输期报出）。
2. URL 与 header name/value 不得含 CRLF；header name 不得为空。
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
