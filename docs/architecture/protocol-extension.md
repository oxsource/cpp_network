# Protocol Extension Mechanism（同步）

**Branch**: `001-cpp-network-library` | **Date**: 2026-08-26（同步重构版）

**对应需求**: FR-021（协议无关抽象层）、FR-017（未来 WebSocket）

**用户故事**: US4 (P3) — WebSocket Communication

**相关设计**: [sync-engine.md](sync-engine.md)、[websocket-api.md](websocket-api.md)、[http-client-api.md](http-client-api.md)

## Overview

设计"新增协议"的接入路径，确保 HTTP（v1）与 WebSocket（v2）及其他未来协议共用同一同步引擎基础设施，且新协议接入不改动既有模块。核心原则：**引擎复用 + 协议层独立 + 公共 API 各成文件**。

## 分层架构

```text
┌─────────────────────────────────────────────┐
│  Public API (netlib)                        │
│  ├── http/   : HttpClient, HttpRequest, ... │
│  └── ws/     : WebSocket (v2)               │
├─────────────────────────────────────────────┤
│  Protocol Layer (src/)                      │
│  ├── http/   : HttpClient 实现, 选项映射    │
│  └── websocket/ : WebSocket 实现(同步收发)  │
├─────────────────────────────────────────────┤
│  Engine (src/http/sync_engine.*)            │
│  └── SyncEngine（共享 CURLM + curl_multi_poll）│
└─────────────────────────────────────────────┘
```

**依赖方向**（严格单向）：
- `SyncEngine` ← 协议层 ← Public API
- 协议层（http/ws）**互不依赖**

## SyncEngine 的协议中立性

`SyncEngine` 提供通用传输驱动（同步、锁保护、连接池），对**具体协议**中立：

```cpp
// src/http/sync_engine.h
struct TransferOptions {
  // 协议层注入的 easy 配置回调：由协议层设置 CURLOPT（HTTP: URL/headers/body;
  // WS: CONNECT_ONLY + curl_ws_* 读写钩子）。
  std::function<CURLcode(CURL* easy, void* userdata)> configure;
  // 完成回调：协议层解析结果（HTTP: 提取 HttpResponse; WS: 记录连接建立）。
  std::function<void(CURL* easy, CURLcode rc, void* userdata)> on_done;
  void* userdata;   // 协议层上下文
};

class SyncEngine {
 public:
  // 同步执行一个传输，阻塞调用线程。协议层通过回调完成读写。
  Result<void> Perform(TransferOptions opts);
};
```

- `SyncEngine` **不知道** HttpResponse / WsMessage；只负责：共享 CURLM + 锁 + `curl_multi_poll` 驱动 + 完成回调。
- 协议的读写/解析逻辑在协议层自己的回调中实现。

## 新协议接入步骤（以 WebSocket 为例）

1. **创建协议层**：`src/websocket/`，实现 `WebSocket::Impl`（持有 easy + 协议上下文）。
2. **复用引擎**：`SyncEngine::Perform` 传 `configure`（设 `CURLOPT_CONNECT_ONLY=2`、绑定 `curl_ws_*` 读写）+ `on_done`。
3. **复用配置**：`NetworkConfig`（timeout/TLS/代理）直接使用。
4. **公共 API**：`netlib::WebSocket`（websocket-api.md）。
5. **BUILD**：`src/websocket/BUILD.bazel` 依赖 `//src/http:sync_engine` + `//src/tls`。

## 约束与不变量

1. **引擎中立**：新增协议不修改 `SyncEngine`（传输驱动对协议透明）。
2. **同步一致**：所有协议的公共 API 均为同步阻塞、返回 `Result<T>`。
3. **错误一致**：所有失败经 `Error`（core-error.md 可追加协议错误码，仅 append）。
4. **单一 CURLM**：HTTP 与 WS 共享同一 `SyncEngine` 的 CURLM（`curl_multi_poll` 锁内驱动全部传输）。

## 错误码扩展策略

- 新协议（WS）新增错误码**追加**到 `ErrorCode` 枚举（如 `kWebSocketProtocolError`），绝不重排既有值。
- 网络层错误码（`kConnectionTimeout` 等）HTTP/WS 共享。

## 评审要点

1. `TransferOptions`（configure/on_done/userdata）抽象是否足够通用（HTTP 完成 vs WS 建立+收发）？
2. `SyncEngine` 是否完全协议无关（无 HttpResponse/WsMessage 引用）？
3. 新协议是否零修改 SyncEngine？（评审 gate）
4. 共享 CURLM 多协议驱动（锁内 curl_multi_poll）是否满足 FR-021？
