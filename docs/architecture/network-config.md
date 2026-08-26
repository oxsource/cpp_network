# Options 配置实体（已实现）

**Branch**: `003-http-implementation` | **Date**: 2026-08-26

**对应需求**: FR-004（timeout）、FR-008（重定向）、FR-010（代理）、FR-012（连接池）、FR-019/TLS

**实现位置**: `src/public/include/http/options.h`、`src/http/options.cc`

> 取代 001 设计稿的 `NetworkConfig` + `HttpClient::Config` 双层设计。落地为单一可变值类型 `Options`：链式 setter 直接修改字段，`Client::Create(options)` 时校验。无 RetryPolicy 字段（见 retry-policy.md）。

## Overview

`cpp_network::http::Options` 是 `Client` 的统一配置实体，内嵌 `Tls`。所有字段在 `Create()` 时经 `Validate()` 校验，映射见 http-config-mapping.md。

## 实际类型定义

```cpp
struct Proxy {
  std::string host;
  uint16_t port = 8080;
};

class Options {
 public:
  // 链式 setter（返回 Options&）与只读访问器一一对应：
  Options& SetConnectTimeout(ms);        // 默认 10s
  Options& SetReadTimeout(ms);           // 默认 30s
  Options& SetWriteTimeout(ms);          // 默认 30s
  Options& SetTotalTimeout(ms);          // 默认 0 = 不限
  Options& SetFollowRedirects(bool);     // 默认 true
  Options& SetMaxRedirects(int);         // 默认 20
  Options& SetInterface(name);           // 指定网卡
  Options& SetLocalAddress(ip);
  Options& SetLocalPort(port);
  Options& SetProxy(host, port);
  Options& SetMaxConnectionsPerHost(n);  // 默认 5
  Options& SetKeepAlive(ms);             // 默认 120s（TCP keepalive 空闲窗口）
  Options& SetTls(const Tls&);

  Result<void> Validate() const;         // 含 Tls::Validate()
};
```

## 校验规则（Validate()）

1. 四类 timeout ≥ 0。
2. `max_redirects ≥ 0`。
3. `interface` / `local_address` 不含 CRLF。
4. `local_port ∈ [0, 65535]`。
5. proxy：host 非空、不含 CRLF；port ≠ 0。
6. `max_connections_per_host ≥ 1`。
7. `keep_alive ≥ 0`。
8. TLS：委托 `Tls::Validate()`（CA 来源互斥、mTLS 成对、SNI 无 CRLF、PEM 形态检查——见 tls-config.md）。

失败返回 `Error(kInvalidArgument, message)`。

## 默认值策略

| 字段 | 默认 | 说明 |
|------|------|------|
| connect_timeout | 10s | |
| read_timeout | 30s | 低速率空闲检测 |
| write_timeout | 30s | 兼作硬超时兜底上限（http-config-mapping.md） |
| total_timeout | 不限 (0) | |
| follow_redirects / max_redirects | true / 20 | |
| proxy | 无 | 直连 |
| max_connections_per_host | 5 | |
| keep_alive | 120s | TCP keepalive 探测空闲窗 |
| tls.verify_mode | kVerifyPeer | 安全默认 |

## 生命周期与不可变性

- `Options` 本身可变；传给 `Client::Create` 后由 Engine **持有拷贝**（也是内联 PEM blob 数据的生命周期锚点），后续修改原对象不影响已创建的 Client。
- 修改配置需重建 Client（v1 不热更新）。
- 请求级覆盖仅 `Request::Timeout`，优先级最高。
