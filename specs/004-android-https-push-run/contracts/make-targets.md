# Contract: make 目标（构建 / push / run）

**Branch**: `004-android-https-push-run`

Makefile 顶层入口经由 `mk/android.mk` 注册；目标名、变量与退出码为对外契约，任务实现不得随意更改。

## Targets

| Target | 作用 | 前置条件 | 成功判定 |
|--------|------|----------|----------|
| `make build-android` | 构建 Android arm64-v8a 的 cpp_network 与设备端可执行程序（device_e2e、http_demo） | `ANDROID_NDK_HOME` 指向 NDK r26+ | bazel build 退出码 0，产物在输出树 |
| `make push [DEVICE=<serial>]` | 解析最新产物并推送二进制 + `src/tests/certs/` 到设备 `/data/local/tmp/cpp_network/` | 至少一台可用设备；产物为非 stale | adb push 全部成功 |
| `make run [DEVICE=<serial>]` | 远端执行 device_e2e（默认外网 HTTPS 场景）→ 输出实时回传 | push 已完成或自动触发 | e2e 退出码透传为 make 退出码 |
| `make clean-device [DEVICE=<serial>]` | 删除设备端 `$DEVICE_DIR` 内容 | 可用设备存在 | 清理成功（目录不存在亦视为成功） |
| `make verify-android [DEVICE=<serial>]` | **一键证书验证**：build → push → local(S1–S7 经 reverse) → external(E1–E3 注入系统信任 bundle) 全链顺序执行 | NDK r26+ 与一台已授权设备 | 两段 run 退出码均 0，`PASS 7/7` + `PASS 3/3` |

## Variables

| 变量 | 默认 | 说明 |
|------|------|------|
| `DEVICE` | 自动单选 | adb 序列号；显式指定时仅作用于该设备（`adb -s`） |
| `ANDROID_SERIAL` | （未设） | 兼容 adb 原生环境变量，优先级低于 `DEVICE` |
| `DEVICE_DIR` | `/data/local/tmp/cpp_network/` | 设备端工作目录 |
| `NETLIB_TEST_EXT_BASE` | 设备侧默认（example.com） | 外网场景可定向到可达端点（如内网 HTTPS 服务） |

> 自动化行为：`run` 检测到远端产物缺失时自动执行 push（等效于先跑一次 `make push`），符合 Targets 表中"push 已完成或自动触发"的约定。

## Device 选择规则

1. 未连接任何设备（含全部 `unauthorized/offline`）→ 立即失败：提示检查 USB 连接。
2. 存在 `unauthorized/offline` 条目且无可用设备 → 失败并列出这些条目（区分"未插"与"未授权"）。
3. 恰好一台 `device` → 直接使用。
4. 多台且未指定 `DEVICE`/`ANDROID_SERIAL` → 列出全部候选序号后终止，不猜测。

## Exit Codes

- make 自身的前置失败（设备选择失败、构建失败）按惯例返回非零
- `run` 必须将设备端程序的退出码原样透传：约定设备端 shell 追加 `echo "EXIT:<code>"`，宿主侧解析后作为最终退出码并从输出中剥离该行（实现哨兵 `__NETLIB_EXIT__:`）
- 环境前提缺失（如无可用设备）→ 快速失败并给出排查指引（实测 11ms）

## 输出契约

- `push` 打印每个传输文件的字节摘要行，便于日志比对；推送后逐项远端存在性断言（防半推送状态，掉线重试即恢复）
- `run` 中设备输出不加前缀直接透传 stdout/stderr（实时流），结束打印 `[device-exit: <code>]` 摘要行
- 所有目标的帮助描述注册进既有 `mk/help.mk` 机制（`make help` 可见）
