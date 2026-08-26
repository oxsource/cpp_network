# ADR-002: HTTP 引擎选型 — libcurl

**Status**: Accepted
**Date**: 2026-08-26
**决策依据文档**: [research.md](../../../specs/001-cpp-network-library/research.md) Decision 2

## Context

需要 HTTP/1.1（v1）+ WebSocket（v2）协议支持，并满足跨平台（macOS/Linux/Android）、TLS 平台适配（OpenSSL/BoringSSL）、连接池复用、重定向/代理/流式等需求。选择协议引擎方案。

## Decision

使用 **libcurl**（≥7.86，WebSocket 支持）作为协议引擎。库封装 libcurl 提供同步 API。TLS 经 libcurl 构建时 SSL 后端选择（host=OpenSSL, Android=BoringSSL）。HTTP 解析、连接池、重定向、代理、chunked、WebSocket 全部委托 libcurl。

## Alternatives Considered

| 备选方案 | 被否决原因 |
|----------|-----------|
| 从零实现 HTTP/1.1 | 工作量大、协议边界 bug 多、重复造轮子 |
| 轻量 parser（llhttp）+ 自研传输 | 仍需自研连接池/重定向/代理/流式 |
| 自研 TlsAdapter 接口 | 重复 libcurl 的 SSL 后端抽象，接口维护成本高 |

## Consequences

- 库本体很薄：HttpClient 同步 API + SyncEngine 共享 CURLM 驱动 + TlsConfig 映射。
- libcurl 为私有实现依赖，其类型绝不进入公共 API（FR-016）。
- 依赖 libcurl 版本演进（WebSocket 需 7.86+）；BoringSSL/OpenSSL 版本锁定见 host/android 构建文档。
