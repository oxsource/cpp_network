# Data Model: specs/006-libwebsockets-wss

日期：2026-08-27。实体均属 `cpp_network::http`（FR-010 命名决策）。

## 实体

### WebSocket（会话句柄）
| 字段 | 类型/约束 | 说明 |
|------|-----------|------|
| state | enum {CONNECTING, OPEN, CLOSING, CLOSED} | 单调推进，CLOSED 终态；幂等关闭入 CLOSING 幂等区 |
| scheme | enum {kWs, kWss} | 连接期一次性确定，不可变 |
| url | string（`ws(s)://host[:port]/path`） | 仅接受 ws/wss 前缀（FR-001） |
| tls | `Tls`（既有类型复用） | kwss 时生效并已 Validate；kws 时整体短路忽略（澄清 Q2/B） |
| timeouts | 复用 NetworkConfig 超时集合 | 连接超时 + 关闭等待上限（FR-009） |

### WsMessage
| 字段 | 类型 | 约束 |
|------|------|------|
| data | bytes | 零长度合法（边缘用例决议） |
| is_text | bool | 文本/二进制；接收侧不凭空合成零长度消息 |

### WsCloseCode
标准码枚举：1000 Normal、1001 GoingAway、1002 ProtocolError、1003 UnsupportedData、1005 NoStatus、1006 Abnormal（保留可扩展 3000–4999 透传为整型）。

### CloseInfo（错误详情载荷，澄清 Q3/B）
| 字段 | 类型 | 暴露路径 |
|------|------|----------|
| code | uint16（0=未知） | 对端发起关闭后的 Receive 错误详情 |
| reason | string ≤123B | 同上；服务端未提供时为空 |

## 状态转移

```text
Idle ──Connect()──► CONNECTING ──ok──► OPEN ──Close()/对端关──► CLOSING ─► CLOSED
                       │失败            │异常断线                    │限时到
                       ▼               ▼                            ▼
                     CLOSED ◄──────────┴────────────────────────────┘
```
- CONNECTING 内不可 Send/Receive/Close（快速状态错）
- OPEN→Send/Receive 正常；CLOSING/CLOSED 后三者皆快速失败，Close 幂等返回成功或已关闭

## 校验规则（连接前快速失败）

1. scheme ∉ {ws,wss} → kInvalidArgument（零网络活动）
2. TLS 配置未过既有 `Tls::Validate()` → 透传其错误
3. 超时值非法（非正数如显式指定）→ kInvalidArgument

## 与既有模型的关系

- **NetworkConfig/Tls**：完全复用（共享对象仅被读取；不与 HTTP 引擎共享事件环——research D4）
- **Error/Result**：新增 websocket 错误类别字段挂接 CloseInfo；不影响 HTTP 错误枚举取值集
