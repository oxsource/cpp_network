# Device/Host Scenario Contract: WSS 场景扩展（device_e2e 协议 v2 追加）

既有 S1–S7/E1–E3 输出行、`PASS n/m` 与 `device-exit: code` 协议**不变**；本契约仅定义追加段。工具 `tools/android_device.sh` 向后兼容，新增可选设备包文件挂载。

## 场景表（追加）

| ID | 模式 | 输入 | 判定 |
|----|------|------|------|
| W1 | external+wss | 公网 wss echo（受信 CA） | 连接成功+文本回声一致 |
| W2 | local+ws | on-device fixture ws echo | 连接成功+二进制回声一致 |
| W3 | external+wss | 自签源默认配置 | kCertificateVerificationFailed |
| W4 | local+wss | 内存 PEM 注入自签 fixture | 回声成功 |
| W5 | both | scheme=`http://…` | 快速失败 kInvalidArgument |
| W6 | local+ws | 8MB 二进制载荷 | 完整回声一致（分段透明） |
| W7 | local+wss mTLS | 无客户端证书→拒；带 PEM →过 | 两段判定同 HTTPS S4/S5 对齐 |
| W8 | local+wss | fixture 主动 close(1000,"bye") | Receive 详情码=1000 reason=bye |

## 输出格式

```text
[W1] PASS : wss public echo round-trip
...
PASS 3/3        # 分组小计与 S/E 同型
[device-exit: 0]
```

- 新场景失败不改变退出码协议：任一 FAIL → device-exit 非 0
- 设备侧 fixture 进程由 android_device.sh 统一管理生命周期（沿用 stage_system_ca_bundle、TMPDIR 约束不变——PEM 内存直达后不再有临时文件依赖）
