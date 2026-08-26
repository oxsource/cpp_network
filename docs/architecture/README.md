# Architecture Documentation

**Feature Branch**: `001-cpp-network-library`

本文档目录承载 C++ 跨平台网络库的**架构设计文档**。本阶段仅产出设计文档，不包含代码实现。

## 目录结构

```text
docs/architecture/
├── README.md                  # 本文档：规范与导航
├── adr/                       # Architecture Decision Records
│   └── adr-XXX-title.md       # 编号 ADR 文档
├── bazel-platforms.md         # Bazel 工作区与平台定义设计
├── core-error.md              # 错误码体系设计
├── sync-engine.md             # 同步传输引擎设计（共享 CURLM + curl_multi_poll，连接池复用）
├── tls-backend-selection.md   # 构建时 TLS 后端选型设计
├── http-client-api.md         # HttpClient 同步 axios 风格 API 设计
├── http-request.md            # HttpRequest 值类型设计
├── http-response.md           # HttpResponse 值类型设计
├── http-transfer-lifecycle.md # HTTP 传输生命周期设计（同步）
├── http-config-mapping.md     # NetworkConfig → libcurl 映射设计
├── tls-config.md              # TlsConfig 类型设计
├── tls-cert-validation.md     # 证书校验流程设计
├── android-boringssl-build.md # （⚠️废弃，历史）Android BoringSSL 构建集成 — 已改全平台 OpenSSL
├── host-openssl-build.md      # host OpenSSL 构建集成
├── network-config.md          # NetworkConfig 实体设计
├── retry-policy.md            # 重试策略设计（上层实现）
├── proxy-config.md            # 代理配置设计
├── connection-pool.md         # 连接池调优设计
├── websocket-api.md           # WebSocket API 设计（同步）
├── protocol-extension.md      # 协议扩展机制设计
├── websocket-message-flow.md  # WebSocket 消息流设计
└── requirement-traceability.md # 需求追踪矩阵
```

> **已移除**：`core-executor.md` / `core-promise.md` / `curl-engine-bridge.md`（异步抽象，2026-08-26 同步 API 重构后合并为 `sync-engine.md`）。

## 设计文档规范

### 统一格式

每个设计文档必须包含以下章节：

1. **标题**：`# <标题>`（含所属分支引用）
2. **元信息**：Branch / Date / 对应需求（FR 编号）与用户故事（US 编号）
3. **Overview**：设计目标一句话概括
4. **设计细节**：接口签名 / 类型定义 / 流程图（代码块）
5. **与既有决策的关系**：引用 research.md 决策编号、其他设计文档
6. **边界与约束**：明确不在本设计范围内的内容
7. **评审要点**：设计评审时需重点核对的事项（对应验收场景）

### 命名约定

- 文件名：小写连字符（`kebab-case`），与 tasks.md 中任务指明的路径一致
- 文档内标题：Sentence case
- 代码片段：C++17 语法，遵守 Google C++ Style Guide

## ADR 模板

ADR 用于记录**已定案的架构决策**（含被否决的备选方案）。编号从 `adr-001` 递增。模板：

```markdown
# ADR-XXX: <决策标题>

**Status**: Accepted | Proposed | Deprecated
**Date**: YYYY-MM-DD
**决策依据文档**: [research.md](../../../specs/001-cpp-network-library/research.md)

## Context

[决策背景：要解决什么问题、有哪些约束]

## Decision

[选定方案，引用对应设计文档]

## Alternatives Considered

| 备选方案 | 被否决原因 |
|----------|-----------|
| ...      | ...       |

## Consequences

[该决策带来的影响、后续需注意的约束]
```

## 与 Spec 的关系

所有设计文档必须可追溯到 spec.md 的功能需求（FR-xxx）与用户故事（USx）。最终由 `requirement-traceability.md` 统一维护追踪矩阵。
