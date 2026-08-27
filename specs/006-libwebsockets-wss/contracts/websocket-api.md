# Public API Contract: WebSocket（ws + wss）

头文件：`src/public/include/ws/websocket.h`；命名空间 `cpp_network::ws`（与 http 平级模块）；导出宏 `CPP_NETWORK_WS_EXPORT`。同步阻塞、无回调（spec FR-002）。

```cpp
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "http/network_config.h"
#include "comm/result.h"

namespace cpp_network {
namespace ws {

struct NETLIB_API WsMessage {
  std::vector<uint8_t> data;  // 载荷字节；零长度合法
  bool is_text = true;        // true=文本帧, false=二进制帧
};

enum class NETLIB_API WsCloseCode : uint16_t {
  kNormal       = 1000,
  kGoingAway    = 1001,
  kProtocolError= 1002,
  kUnsupportedData = 1003,
  kNoStatus     = 1005,
  kAbnormal     = 1006,
};

class NETLIB_API WebSocket {
 public:
  // 仅接受 ws:// 与 wss://；其余 scheme 零网络活动快速失败。
  // wss: 应用传入 Tls 配置（默认系统信任策略与 HTTP 一致）。
  // ws:  TLS 配置静默忽略。
  static Result<WebSocket> Connect(const std::string& url,
                                   const NetworkConfig& config);

  bool IsOpen() const;  // 仅 OPEN 为 true（CONNECTING/CLOSING/CLOSED 均 false）

  Result<void> Send(const WsMessage& msg);      // 完整消息原子语义；分段透明
  Result<WsMessage> Receive();                  // 阻塞取下一条完整消息；
                                                // 探活帧自动应答不入消息流
  Result<void> Close(WsCloseCode code, const std::string& reason);

 private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
  explicit WebSocket(std::shared_ptr<Impl> impl);
};

}  // namespace ws
}  // namespace cpp_network
```

## 错误语义契约

| 场景 | Error code | 附加载荷 |
|------|-----------|----------|
| scheme 非法 | kInvalidArgument | — |
| TLS 校验失败/自签拒默认 | kCertificateVerificationFailed（与 HTTP 同码） | — |
| 连接超时/握手拒绝 | kConnectionFailed / kProtocolError | — |
| 未 OPEN 即操作 | kInvalidState | — |
| 对端已关闭后的 Receive | kConnectionClosed | CloseInfo{code,reason}（Q3/B） |
| 本端重复 Close | 幂等：kOk 或 kInvalidState="already closed" 二选一实现定并文档化，不抛异常路径 |

> error.h 最小扩展：Error 增加可选 `close_code/reason` 字段，仅由 websocket 层填充。

## 行为不变量（验收用）

1. Send 前≥N MB 消息：单次调用返回 Ok，Receive 一次取整条（FR-005）。
2. 全平台场景判定逐项一致（SC-002）。
3. `ws://`+TLS 材料 → 连接成功且无加密（不报错——Q2/B）。
4. 默认配置对自签名 wss → 拒绝（防降级基线）。
