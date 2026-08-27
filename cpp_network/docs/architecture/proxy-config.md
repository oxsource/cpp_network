# 代理配置（已实现）

**Branch**: `003-http-implementation` | **Date**: 2026-08-26

**对应需求**: FR-010（代理路由）

**实现位置**: `src/public/include/comm/options.h`（Proxy）、`src/http/options.cc`（校验）、`src/http/detail/curl_mapping.cc`（映射）

## Overview

`Options::SetProxy(host, port)` 配置 HTTP 代理；映射为单个 `CURLOPT_PROXY` 选项，其余行为委托 libcurl。

## 实际行为

| 项 | 行为 |
|----|------|
| 类型 | 仅 HTTP 代理。`CURLOPT_PROXYTYPE` 未显式设置，依赖 libcurl 默认值（HTTP），功能等效 |
| 映射 | `CURLOPT_PROXY = "<host>:<port>"` |
| HTTPS 目标 | 经 CONNECT 隧道（libcurl 默认） |
| 认证 | v1 不支持（无 `CURLOPT_PROXYUSERPWD`） |

## 校验（Options::Validate）

- host 非空且不含 CRLF
- port ≠ 0（uint16 天然上限 65535）

## 边界与约束

- SOCKS / HTTPS 代理留 polish。
- 无 per-request 代理覆盖（代理属 client 级配置）。
