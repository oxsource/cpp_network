# Architecture Documentation

**Feature Branch**: `001-cpp-network-library`（设计）→ `003-http-implementation`（实现）

本文档目录承载 C++ 跨平台网络库的**架构设计文档**。HTTP/TLS 部分（spec 003）已实现并同步至实际命名体系（`cpp_network::http`：`Client`/`Request`/`Response`/`Options`/`Tls`）；各文档头部状态横幅标明与实现的对应关系。v2 范围（WebSocket、协议中立抽象、重试策略类型）保留草案并加注。

## 目录结构

```text
docs/architecture/
├── README.md                  # This doc: conventions and navigation
├── adr/                       # Architecture Decision Records
│   └── adr-XXX-title.md       # Numbered ADR documents (historical decision records)
├── bazel-platforms.md         # Bazel workspace & platform definitions (implemented; see top banner for diffs)
├── core-error.md              # Error code system (implemented: ErrorCode/Error/Result + MapCurlError)
├── sync-engine.md             # Sync transfer engine (implemented: Engine = shared CURLM + curl_multi_poll)
├── tls-backend-selection.md   # TLS backend selection (all-platform OpenSSL via libcurl; see banner for layout diffs)
├── http-client-api.md         # Client sync API (implemented)
├── http-request.md            # Request value type (implemented)
├── http-response.md           # Response value type (implemented; streaming deferred)
├── http-transfer-lifecycle.md # HTTP transfer lifecycle (implemented)
├── http-config-mapping.md     # Options/Request → libcurl mapping (implemented)
├── tls-config.md              # Tls type design (implemented: in-memory PEM/file path/mTLS/SNI/validation)
├── tls-cert-validation.md     # Certificate validation flow (implemented, incl. test comparison table)
├── android-boringssl-build.md # (⚠️ deprecated, historical) Android BoringSSL build integration — replaced by all-platform OpenSSL
├── host-openssl-build.md      # Host OpenSSL build (source build deferred; currently system -lcurl)
├── network-config.md          # Options config entity (implemented; supersedes NetworkConfig draft)
├── retry-policy.md            # Retry policy (reference for upper layers; no RetryPolicy type in library)
├── proxy-config.md            # Proxy configuration (implemented: HTTP proxy)
├── connection-pool.md         # Connection pool tuning (implemented: delegated to libcurl)
├── websocket-api.md           # WebSocket API design (v2 draft, not implemented)
├── protocol-extension.md      # Protocol extension mechanism (v2 direction, TransferOptions not implemented)
├── websocket-message-flow.md  # WebSocket message flow design (v2 draft, not implemented)
└── requirement-traceability.md # Requirement traceability matrix (incl. spec 003 implementation status)
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
