# WebSocket API 设计（libwebsockets 实现，specs/006）

> 状态：已实现并验证（2026-08-27）。取代 001 v2 草案的 libcurl `curl_ws_*` 方案，
> 决策记录见 [adr-004-websockets-transport.md](adr/adr-004-websockets-transport.md)。

## Overview

基于**源码构建 libwebsockets v4.5.8（静态链接）**的 WebSocket 客户端，同步阻塞形态：
`Connect` / `Send` / `Receive` / `Close` 均阻塞调用线程、经 `Result<T>` 返回。
通道支持 `ws://` 与 `wss://` 双协议（2026-08-27 澄清）；TLS 仅作用于加密通道且
由**同一个 TLS bundle 的 OpenSSL**驱动——`lws_close_reason`/内存锚接口直连
`//third_party/openssl/{host,android}:openssl` 切片，杜绝第二 TLS 栈。

## 公共 API

```cpp
// src/public/include/ws/websocket.h (namespace cpp_network::ws)
struct WsMessage { std::vector<uint8_t> data; bool is_text = true; };

enum class WsCloseCode : uint16_t {
  kNormal = 1000, kGoingAway, kProtocolError, kUnsupportedData,
  kNoStatus = 1005, kAbnormal = 1006,
};

class WebSocket {
 public:
  static Result<WebSocket> Connect(const std::string& url, const Options& opt);
  bool IsOpen() const;
  Result<void> Send(const WsMessage& msg);       // 整条原子语义
  Result<WsMessage> Receive();                   // 探活透明、完整消息
  Result<void> Close(WsCloseCode code, const std::string& reason);
};
```

要点：
- 仅接受 `ws://` / `wss://` 前缀；其余 scheme 零网络活动快速失败 `kInvalidArgument`
- `ws://` 上的 TLS 配置静默忽略（文档契约，防"以为加密了"）
- 关闭详情（对端 code/reason）挂在 `Error::close_code()/close_reason()` 上，
  对端发起关闭后的 Receive 以 `kConnectionClosed` 失败并携带之
- 权限矩阵见 contracts/websocket-api.md；场景映射 W1–W8 见
  contracts/device-scenarios.md

## 内部机制（sync pump）

```text
WebSocket::Impl ──owns──► WsSession { lws_context*, state, rx_queue, tx_* }
Connect : ParseWsUrl → TLS 内存装配(ca/cert/key mem) → lws_client_connect_via_info
          → PumpUntil(ESTABLISHED 或失败分类)
Send    : 装载 tx_payload → callback_on_writable → WRITABLE 回调内一次性提交全部剩余
          （lws 自动分帧；禁止应用层再切窗——证据见 build-matrix.md Phase4 注记 1）
Receive : CLIENT_RECEIVE 按 first/final fragment 组帧入队 → PumpUntil 出队
Close   : 武装 close_pending → WRITABLE 内 lws_close_reason() 且回调返回非零
          （lws-ws-close.h 契约）→ 两阶段握手直至 CLOSED / 超时强制收敛
```

状态机与重连策略同 message-flow 文档；v1 不做自动重连、不做 permessage-deflate
（`LWS_WITHOUT_EXTENSIONS=ON` 构建，从二进制层面排除）。

## 构建接线

```text
cpp_network_deps.bzl (lws pin v4.5.8) ─► third_party/libwebsockets/{host,android}:build_lws genrule
    │  CMake: LWS_WITHOUT_SERVER/EXTENSIONS、静态、指向 bundle OpenSSL 根
    ▼
//third_party/libwebsockets/{host,android}:websockets (cc_library, alwayslink 于 src/websocket)
    ▼
src/websocket/websocket.cc ← select 五平台路由
```

平台纪律与已知坑（symlink glob / make 展开 / dylib dynamic_lookup / 只读 outs /
phantom up-to-date 规避法）沉淀于 specs/006-libwebsockets-wss/evidence/build-matrix.md。
