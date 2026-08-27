# 终验报告（specs/006 WebSocket over libwebsockets）

日期：2026-08-27；执行机：Mac mini M4；真机：be11 (an4009056e01d0d04)

## SC 达成核对

| SC | 判定 | 证据 |
|----|------|------|
| SC-001 全流程环回 p95≤100ms | ✅ | sc1 采样：p50=0.3ms / **p95=0.5ms** / max=1.8ms |
| SC-002 双通道安全基线跨平台一致 | ✅ | 宿主 gtest W2-W5 与真机 local W1-W7 同判（自签拒/注入过/明文通/scheme 快速失败）零分歧 |
| SC-003 既有用例数量不减断言不改 | ✅ | `bazel test //...` 7/7（新增 websocket_test 后套件数 6→7），HTTP 套件断言零改动 |
| SC-004 升级演练 ≤30min | ✅ | 实测 rebuild-all 66s + android_build 79s ≈ **145s** |
| SC-005 真机一条命令 ≥7 场景 | ✅ | `make android_verify` local 14/14 + external 4/4 |

## 关键实施事实（详见 build-matrix.md 各 Phase 注记）

1. `--dynamic_mode=off` 全局固化（dylib 动态模式与三方静态对象不兼容）
2. lws 写管线："单次 writable 提交全部剩余"，手工分窗致帧错位已弃
3. lws_close_reason 需回调返回非零才真正发起两阶段 close
4. CA 三形态路由：mem(单 PEM)/filepath(多证书 bundle, Android 合并库)/skip
5. fixture 修复：>125B 扩展长度 MASK 位、探针预连接韧性、就绪探针反逻辑

## 残留

- permessage-deflate / 自动重连：显式排除（FR-012），后续按需立项
- Linux/macos_x86_64 运行级取证：延续 analysis-only 等级待执行机
