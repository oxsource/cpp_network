# HTTP Proxy Configuration Design

**Branch**: `001-cpp-network-library` | **Date**: 2026-08-26

**对应需求**: FR-010（HTTP 代理配置）

**用户故事**: US3 (P2) — Configure Network Client Settings

**相关设计**: [network-config.md](network-config.md)、[http-config-mapping.md](http-config-mapping.md)

## Overview

设计 HTTP 代理配置：`NetworkConfig::proxy`（host:port），映射到 libcurl `CURLOPT_PROXY`/`CURLOPT_PROXYTYPE`。v1 仅支持 HTTP 代理（含 HTTPS 目标的 CONNECT 隧道）。

## 类型定义

```cpp
// src/public/include/netlib/network_config.h
struct Proxy {
  std::string host;      // 代理主机（不含 scheme）
  uint16_t port = 8080;
};
```

`NetworkConfig::proxy` 为 `std::optional<Proxy>`：
- `nullopt` → 直连（默认）。
- 非空 → 所有请求经该代理。

## 映射到 libcurl

| NetworkConfig 字段 | libcurl 选项 | 值 |
|--------------------|--------------|----|
| `proxy.host` + `proxy.port` | `CURLOPT_PROXY` | `"host:port"`（如 `"proxy.example.com:8080"`） |
| 类型固定 | `CURLOPT_PROXYTYPE` | `CURLPROXY_HTTP` |

### 转发语义

| 目标 scheme | libcurl 行为 |
|-------------|--------------|
| `http://` | 直接向代理发绝对 URL 请求（HTTP 转发） |
| `https://` | 通过代理 CONNECT 隧道建立 TLS（libcurl 自动处理） |

## 校验规则（network-config.md 联动）

- `host` 非空、不含空白/`\r`/`\n`。
- `port ∈ [1, 65535]`。
- 不校验代理可达性（运行时失败以 `kConnectionRefused` 等错误反映）。

## 代理认证（v1 不做）

- `CURLOPT_PROXYUSERPWD`（代理用户名/密码）**不在 v1 范围**（YAGNI）。
- 文档标注：需要认证的代理 v1 不支持，留 polish。

## 测试/验收场景对照（spec US3 场景 3）

| 配置 | 行为 |
|------|------|
| `SetProxy("proxy.example.com", 8080)` + 请求 `http://example.com` | 请求经 `proxy.example.com:8080` 转发（`CURLOPT_PROXY = "proxy.example.com:8080"`） |

## 边界与约束

- 仅 HTTP 代理类型（`CURLPROXY_HTTP`）；SOCKS4/5、HTTPS 代理留 polish。
- 代理对 `https://` 目标走 CONNECT 隧道，不做 TUNNEL 关闭（`CURLOPT_HTTPPROXYTUNNEL` 默认 libcurl 行为保持）。
- 不做 per-host 代理切换（全局代理）。

## 评审要点

1. `https://` 目标的 CONNECT 隧道语义是否明确（libcurl 默认行为）？
2. 代理认证缺失的 v1 限制是否文档化？
3. 校验（host/port）是否完整？
