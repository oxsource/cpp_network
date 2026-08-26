# WebSocket Message Flow & Reconnect Design（同步）

**Branch**: `001-cpp-network-library` | **Date**: 2026-08-26（同步重构版）

**对应需求**: FR-017（message send/receive/close）

**用户故事**: US4 (P3) — WebSocket Communication

**相关设计**: [websocket-api.md](websocket-api.md)、[protocol-extension.md](protocol-extension.md)、[sync-engine.md](sync-engine.md)

## Overview

设计 WebSocket 的同步消息收发流、状态机与断线处理（含可配置重连）。消息经 libcurl `curl_ws_*` API 收发，帧重组/分段在协议层处理；**全部同步阻塞**，无事件回调——上层如需异步，用线程池/协程包装。

## 状态机

```text
        Connect() 成功          Close() / 对端关闭 / 错误
Idle ───────► CONNECTING ─────► OPEN ───────────────────► CLOSING ─► CLOSED
                  │ 失败          │ 断线(异常)                │
                  └────────► CLOSED ◄────────────────────────┘
                             （可触发重连 → 回到 CONNECTING）
```

- **CONNECTING**：HTTP Upgrade 握手进行中（`Connect` 阻塞直到完成）。
- **OPEN**：可收发消息。
- **CLOSING**：已发 Close 帧，等待对端 close 或超时。
- **CLOSED**：连接终结。意外断线（非正常关闭）时若配置重连则回 CONNECTING。

## 消息收发流（同步）

### 发送（Send）

```text
Send(msg)
 → state_==OPEN? 否则返回 Result<Error(kInvalidState)>
 → curl_ws_send(easy, msg.data, size, &sent, flags)
 → sent 全写完? Result<void>::Ok : 循环继续（阻塞驱动）
 → 错误 → Result<Error>
```

### 接收（Receive）

```text
Receive()
 → 阻塞驱动 curl_multi_poll（sync-engine.md 方式）
 → curl_ws_recv(easy, buf, BUFSZ, &recv, &meta)
 → meta.flags & CURLWS_PING → 自动回 PONG（透明）
 → meta.flags & CURLWS_PONG → 丢弃（内部心跳）
 → meta.flags & CURLWS_CLOSE → 走关闭流程 → 返回 Result<Error> 或状态标志
 → meta.flags & CURLWS_CONT → 帧续段：拼入 recv_buffer_
 → meta.flags & CURLWS_TEXT/BINARY → 完整帧 → Result<WsMessage>::Ok
 → recv == 0（无数据）→ 继续阻塞驱动直到有数据或超时
```

## 控制帧处理

| 控制帧 | 处理 |
|--------|------|
| `CURLWS_PING` | 自动回 `PONG`（库内透明处理） |
| `CURLWS_PONG` | 丢弃（应用层不可见） |
| `CURLWS_CLOSE` | 回 Close 帧 → CLOSED（正常关闭码） |

## 断线重连（上层或库内封装，同步）

### 触发条件

- 意外断线（网络中断、`kConnectionClosed`/`kConnectionTimeout`，**非**正常 Close）。
- 服务端无响应超时（可选，v2）。

### 重连策略（同步辅助函数）

```cpp
// 库可提供一个同步重连辅助（或上层自行循环）：
struct WsReconnectOptions {
  bool enabled = false;
  int max_attempts = 3;
  std::chrono::milliseconds backoff{1000};
};

Result<WebSocket> ConnectWithRetry(const std::string& url,
                                   const NetworkConfig& config,
                                   const WsReconnectOptions& opts);
```

流程：

```text
for attempt in 0..max_attempts:
  auto ws = WebSocket::Connect(url, config);
  if (ws.ok()) return ws;
  if (!opts.enabled || attempt == max_attempts-1) return ws;   // 最后一次
  std::this_thread::sleep_for(opts.backoff * attempt);         // 简单退避
return 最后一次错误;
```

- 阻塞期间由调用线程 sleep（或上层用线程池/调度器替代 sleep）。
- 运行中断线重连：由上层监听（轮询 `IsOpen()` / `Receive` 错误）后重新 `ConnectWithRetry`。

### 与 spec US4 场景 2 对照

| 场景 | 行为 |
|------|------|
| 连接中断 + 配置重连 | 上层/辅助函数按 backoff 重新 `Connect`，成功则重入 OPEN |
| 连接中断 + 未配置重连 | `Receive`/`Send` 返回连接类错误，上层自行决定重建 |

## 线程与并发

- `Connect`/`Send`/`Receive`/`Close` 可从任意线程调用；内部以 `state_` + mutex 保护（与 SyncEngine 同锁）。
- 全部同步阻塞；无回调派发、无事件循环。

## 边界与约束

- v1 不实现 WebSocket；本设计为 v2 接入路径（protocol-extension.md）。
- 心跳（PING 自动应答）由库处理；主动心跳间隔（`ws_ping_interval`）v2 可选。
- 指数退避由上层扩展（v2 提供简单线性退避辅助）。
- 大消息分片：`recv_buffer_` 按需增长，无硬上限（内存风险由上层控制，文档标注）。

## 评审要点

1. 控制帧（PING/PONG/CLOSE/CONT）处理是否完整且对应用透明？
2. 同步阻塞的收发模型是否清晰（调用线程驱动）？
3. 重连触发条件（异常断开 vs 正常关闭）区分是否正确？
4. 重连辅助的退避/sleep 是否与"库内无线程"约束一致（阻塞调用线程）？
