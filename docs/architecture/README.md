# Architecture Documentation

**Feature Branch**: `001-cpp-network-library`（设计）→ `003-http-implementation`（实现）

本文档目录承载 C++ 跨平台网络库的**架构设计文档**。HTTP/TLS 部分（spec 003）已实现并同步至实际命名体系（`cpp_network::http`：`Client`/`Request`/`Response`/`Options`/`Tls`）；各文档头部状态横幅标明与实现的对应关系。v2 范围（WebSocket、协议中立抽象、重试策略类型）保留草案并加注。

## 目录结构

```text
docs/architecture/
├── README.md                  # 本文档：规范与导航
├── adr/                       # Architecture Decision Records
│   └── adr-XXX-title.md       # 编号 ADR 文档（历史决策记录）
├── bazel-platforms.md         # Bazel 工作区与平台定义（已落地，差异见文首横幅）
├── core-error.md              # 错误码体系（已实现：ErrorCode/Error/Result + MapCurlError）
├── sync-engine.md             # 同步传输引擎（已实现：Engine = 共享 CURLM + curl_multi_poll）
├── tls-backend-selection.md   # TLS 后端选型（全平台 OpenSSL 经 libcurl；布局差异见横幅）
├── http-client-api.md         # Client 同步 API（已实现）
├── http-request.md            # Request 值类型（已实现）
├── http-response.md           # Response 值类型（已实现；流式 deferred）
├── http-transfer-lifecycle.md # HTTP 传输生命周期（已实现）
├── http-config-mapping.md     # Options/Request → libcurl 映射（已实现）
├── tls-config.md              # Tls 类型设计（已实现：内存 PEM/文件路径/mTLS/SNI/校验）
├── tls-cert-validation.md     # 证书校验流程（已实现，含测试对照表）
├── android-boringssl-build.md # （⚠️废弃，历史）Android BoringSSL 构建集成 — 已改全平台 OpenSSL
├── host-openssl-build.md      # host OpenSSL 构建（源码构建为后续任务；当前系统 -lcurl）
├── network-config.md          # Options 配置实体（已实现；取代 NetworkConfig 设计稿）
├── retry-policy.md            # 重试策略（上层实现参考；库内无 RetryPolicy 类型）
├── proxy-config.md            # 代理配置（已实现：HTTP 代理）
├── connection-pool.md         # 连接池调优（已实现：委托 libcurl）
├── websocket-api.md           # WebSocket API 设计（v2 草案，未实现）
├── protocol-extension.md      # 协议扩展机制（v2 方向，TransferOptions 未实现）
├── websocket-message-flow.md  # WebSocket 消息流设计（v2 草案，未实现）
└── requirement-traceability.md # 需求追踪矩阵（含 spec 003 实现状态说明）
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
