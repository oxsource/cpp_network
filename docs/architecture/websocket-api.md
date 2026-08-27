# WebSocket API Design（同步版）

> **状态（2026-08-26，spec 003 实现核对）**：WebSocket 为 v2 范围，`src/websocket/` 目前仅有占位 BUILD 目标。命名体系已迁移至 `cpp_network`（本文仍为 001 草案的 `netlib` 命名），实现时需按新命名重新起草。

**Branch**: `001-cpp-network-library` | **Date**: 2026-08-26（同步重构版）

**对应需求**: FR-017（WebSocket：upgrade/message/close）、FR-018（修订：同步）

**用户故事**: US4 (P3) — WebSocket Communication

**相关设计**: [protocol-extension.md](protocol-extension.md)、[websocket-message-flow.md](websocket-message-flow.md)、[sync-engine.md](sync-engine.md)

## Overview

基于 **libcurl 7.86+ WebSocket API**（`CURLOPT_CONNECT_ONLY` + `curl_ws_*`）的 WebSocket 客户端，**同步阻塞**形态：`Connect`/`Send`/`Receive`/`Close` 均阻塞调用线程，直接返回结果。复用既有 `HttpClient`/`SyncEngine`/`TlsConfig`。v1 为 HTTP；本设计为 v2 预留。

## 依赖前提

- libcurl ≥ 7.86 启用 WebSocket（`curl_ws_*` API）。
- TLS：`wss://` 沿用 libcurl SSL 后端（OpenSSL，全平台），与 HTTP 一致（tls-backend-selection.md）。
- 复用 `HttpClient` 的 `SyncEngine`（共享 CURLM + curl_multi_poll）与 `NetworkConfig`。

## 公共 API 设计（v2 草案，同步）

```cpp
// src/public/include/netlib/websocket.h
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "netlib/network_config.h"
#include "netlib/result.h"

namespace netlib {

struct WsMessage {
  std::vector<uint8_t> data;   // Raw bytes
  bool is_text = true;         // text vs binary frame
};

enum class WsCloseCode {
  kNormal = 1000, kGoingAway = 1001, kProtocolError = 1002,
  kUnsupportedData = 1003, kNoStatus = 1005, kAbnormal = 1006,
};

class WebSocket {
 public:
  // Establishes a connection (HTTP Upgrade). Blocks until the handshake completes.
  static Result<WebSocket> Connect(const std::string& url,
                                   const NetworkConfig& config);

  bool IsOpen() const;

  // Sends a message (blocks until the write completes).
  Result<void> Send(const WsMessage& msg);

  // Receives the next frame (blocks until one arrives). Returns the message on success.
  Result<WsMessage> Receive();

  // Sends a close frame; waits for the peer to close or for timeout.
  Result<void> Close(WsCloseCode code, const std::string& reason);

  ~WebSocket();

 private:
  friend class WebSocketFactory;
  struct Impl;
  std::shared_ptr<Impl> impl_;
};

}  // namespace netlib
```

## 同步语义对应（JS WebSocket 类比）

| WebSocket（JS 类比） | 本库（同步） |
|----------------------|--------------|
| `new WebSocket(url)` + `onopen` | `WebSocket::Connect(url, config)` 阻塞返回 |
| `ws.send(data)` | `WebSocket::Send(msg)` |
| `ws.onmessage` | `WebSocket::Receive()` 阻塞取下一帧 |
| `ws.onclose` | `WebSocket::Close()` / `Receive` 返回关闭 |

## 内部实现（Impl）

```text
WebSocket::Impl
 ├── easy_          : CURL*（CONNECT_ONLY 模式，一个 easy 承载一个 ws 连接）
 ├── engine_        : SyncEngine（复用共享 CURLM，见 sync-engine.md）
 ├── state_         : CONNECTING / OPEN / CLOSING / CLOSED
 └── recv_buffer_   : 未读帧缓冲（帧分段重组）
```

关键 libcurl 调用（protocol-extension.md 详述）：
- 连接：`CURLOPT_CONNECT_ONLY = 2L`（WebSocket）+ `CURLOPT_HTTP_VERSION = CURL_HTTP_VERSION_1_1`。
- 发送：`curl_ws_send(easy, data, len, &sent, 0)`。
- 接收：`curl_ws_recv(easy, buf, buflen, &recv, &meta)`（`meta.flags` 判帧边界/类型）。

## 与 HTTP 的代码复用

- `SyncEngine`：多路复用（一个 CURLM 同时承载 HTTP 传输与 WS 连接），`curl_multi_poll` 阻塞驱动。
- `NetworkConfig`：timeout/TLS/代理直接复用。
- `Result`：`Connect`/`Send`/`Receive`/`Close` 全部返回 `Result<T>`。

## 边界与约束

- v1 不实现 WebSocket（本设计为 v2 预留）。
- 不支持压缩扩展（permessage-deflate，v2 可后开）。
- 不支持 `ws://` 明文在生产建议（与 HTTP 一致安全默认）。

## 评审要点

1. API 是否全同步（无回调/事件）且贴合 WebSocket 语义？
2. `Receive()` 阻塞取帧的调用模型是否清晰（调用线程驱动）？
3. `wss://` TLS 是否无缝复用 HTTP 的 SSL 后端选型？
4. 复用 SyncEngine 的共享 CURLM（HTTP + WS 同 multi）是否有设计约束冲突？
