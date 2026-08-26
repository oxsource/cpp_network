# 连接池调优（已实现）

**Branch**: `003-http-implementation` | **Date**: 2026-08-26

**对应需求**: FR-012（连接池）、SC-004（连接复用）

**实现位置**: `src/http/engine.cc`、`src/http/detail/curl_mapping.cc`（ApplyMultiOptions）、[network-config.md](network-config.md)

## Overview

不自研连接池：共享 `CURLM*` 的 libcurl 连接缓存承担全部复用语义。库只暴露一个调优旋钮 + TCP keepalive 窗口。

## 实际映射

| Options 字段 | libcurl 选项 | 默认 | 说明 |
|--------------|--------------|------|------|
| `max_connections_per_host` | `CURLMOPT_MAX_HOST_CONNECTIONS` | 5 | 单 host 并发连接上限，超出排队 |
| `keep_alive` (>0) | `CURLOPT_TCP_KEEPALIVE=1` + `CURLOPT_TCP_KEEPIDLE=⌈ms⌉s` | 120s | TCP 层 keepalive 探测空闲窗 |

未设置 `CURLMOPT_MAXCONNECTS`——libcurl 默认按需扩展总连接缓存；HTTP keep-alive 与空闲关闭时机由 libcurl 内部管理。

> 相比 001 设计稿的落地差异：不提供 keep_alive → MAXCONNECTS 的启发式映射；错误码 `kConnectionPoolExhausted` 已从枚举删除（排队等待不会以"池耗尽"失败）。

## 复用路径

同一 Client 的多次请求经同一把锁串行进入同一 `CURLM*`；同 host 的后续传输命中缓存连接时免 DNS/TCP/TLS 握手（本地请求毫秒级达成，SC-004 由 http_integration_test 验证）。

## 边界与约束

- 不做 per-host 差异化池配置。
- 不暴露空闲连接数/命中率等观测接口（留 polish）。
