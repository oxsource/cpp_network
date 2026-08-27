# macOS arm64 运行级回归取证（specs/005 Phase 2 / T004-T007）

日期：2026-08-27；翻转提交后基线（源码构建 OpenSSL 3.0.13 + curl 8.7.1，darwin64-arm64-cc，对象层符号隐藏）

| 验证项 | 结果 |
|--------|------|
| gtest 全量（smoke/headers/url/http_integration/https/config） | **6/6 PASSED** |
| device_e2e external E1–E3（系统锚 /etc/ssl/cert.pem 注入） | **PASS 3/3** |
| device_e2e local S1–S7（自签/mTLS fixtures） | **PASS 7/7** |
| 内存注入 S3 的临时文件兜底 | 触发计数 **0**（走 CAINFO_BLOB 原生通道，FR-005 达成）|
| http_demo 四段冒烟 | 全部 status 200 |
| Android 真机 android_verify（共用脚本泛化后复验） | S1–S7 **7/7** + E1–E3 **3/3**，退出码 0 |

## 观察与附注
- macOS 系统 curl 的 BLOB 运行时怪癖随系统链接路径移除而消失：S2/S3 走原生 blob。
- http_demo 原 demo 未注入锚导致默认段失败——已按 FR-003 模式统一注入系统锚，四段全绿。
- Android 端 E1–E3 使用设备侧合并的系统信任 bundle；两侧"同场景同结果"达成 US1 目标。
