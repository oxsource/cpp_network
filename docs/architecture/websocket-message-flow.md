# WebSocket Message Flow & 状态机（同步实现版）

> 状态：随 specs/006 实现更新（2026-08-27）；草案期的 curl_ws 内容废弃。

## 状态机（与 data-model.md 一致）

```text
        Connect() 成功          Close() / 对端关闭 / 错误
Idle ───────► CONNECTING ─────► OPEN ───────────────────► CLOSING ─► CLOSED
                  │ 失败          │ 断线(异常)                │限时到
                  └────────► CLOSED ◄────────────────────────┘
```

- CONNECTING：握手泵进行中（connect_timeout 生效）
- OPEN：Send/Receive 正常；IsOpen()==true 仅此态
- CLOSING：close 帧已提交、限等对端回执（write_timeout 作为关闭等待上限）
- CLOSED：终态。对端主动关闭会先经 PEER_INITIATED_CLOSE 记录 code/reason；
  之后 Receive 返回 kConnectionClosed 并携带详情

## 消息流

### Send
校验 OPEN → tx_payload 载入 → arm writable → WRITABLE 中以
[LWS_PRE|剩余] scratch 一次性 `lws_write`（TEXT/BINARY 首、CONTINUATION 续、
NO_FIN 控制收尾全部交给 lws 内部分帧）→ pump 至 tx_active 清零或错误/超时。
零长度载荷合法直发。

### Receive
CLIENT_RECEIVE 回调按 `lws_is_first_fragment/final_fragment` 组帧整条入队；
PumpUntil 直到队列非空（read_timeout 兜底 kReadTimeout）。PING/PONG 由 lws
核心自动应答，不进消息流（W9 断言）。

### Close
幂等入口（kClosing/kClosed 直接 Ok）→ 武装 code+reason(≤123B) → WRITABLE 中
`lws_close_reason(...)` 且回调返回非零请求关闭 → 两阶段握手；时限到即本地强制
CLOSED 仍返回 Ok（FR-009）。对端拒绝时后续操作走 ClosedStateError 快速失败。

## 错误映射摘要

| 情形 | code | 详情 |
|------|------|------|
| scheme 非法 | kInvalidArgument | — |
| 自签默认拒 | kCertificateVerificationFailed | 分类器词表含 x509_v_err |
| 握手 SSL 层失败 | kTlsHandshakeFailed | — |
| 连接超时 | kConnectionTimeout | — |
| 未 OPEN 操作 | kInvalidState | — |
| 对端已关 | kConnectionClosed | close_code/close_reason |

断线自动重连仍属显式排除项（FR-012），调用方自行外层重试。
