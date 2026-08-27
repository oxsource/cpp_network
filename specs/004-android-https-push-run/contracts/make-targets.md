# Contract: make 目标（构建 / push / run）

**Branch**: `004-android-https-push-run`

Makefile 顶层入口经由 `mk/android.mk` 注册；目标名、变量与退出码为对外契约，任务实现不得随意更改。

## Targets

| Target | 作用 | 前置条件 | 成功判定 |
|--------|------|----------|----------|
| `make build-android` | 构建 Android arm64-v8a 的 cpp_network 与设备端可执行程序（device_e2e、http_demo） | `ANDROID_NDK_HOME` 指向 NDK r26+ | bazel build 退出码 0，产物在输出树 |
| `make push [DEVICE=<serial>]` | 解析最新产物并推送二进制 + `src/tests/certs/` 到设备 `/data/local/tmp/cpp_network/` | 至少一台可用设备；产物为非 stale | adb push 全部成功 |
| `make run [DEVICE=<serial>] [PORTS="18080 18443"]` | 启动宿主测试服务 → 建立端口反向转发 → 设备端执行程序 → 输出回传 | push 已完成或自动触发 | e2e 退出码透传为 make 退出码 |
| `make clean-device [DEVICE=<serial>]` | 删除设备端 `/data/local/tmp/cpp_network/` 内容 | 可用设备存在 | 清理成功（目录不存在亦视为成功） |

## Variables

| 变量 | 默认 | 说明 |
|------|------|------|
| `DEVICE` | 自动单选 | adb 序列号；显式指定时仅作用于该设备（`adb -s`） |
| `ANDROID_SERIAL` | （未设） | 兼容 adb 原生环境变量，优先级低于 `DEVICE` |
| `PORTS` | `"18080 18443"` | 需要反向转发的端口清单（空格分隔） |
| `DEVICE_DIR` | `/data/local/tmp/cpp_network/` | 设备端工作目录 |

## Device 选择规则

1. 未连接任何设备（含全部 `unauthorized/offline`）→ 立即失败：提示检查 USB 连接。
2. 存在 `unauthorized/offline` 条目且无可用设备 → 失败并列出这些条目（区分"未插"与"未授权"）。
3. 恰好一台 `device` → 直接使用。
4. 多台且未指定 `DEVICE`/`ANDROID_SERIAL` → 列出全部候选序号后终止，不猜测。

## Exit Codes

- make 自身的前置失败（设备选择失败、构建失败）按惯例返回非零
- `run` 必须将设备端程序的退出码原样透传：约定设备端 shell 追加 `echo "EXIT:<code>"`，宿主侧解析后作为最终退出码并从输出中剥离该行
- 转发建立失败 / 宿主服务端口被占用 → 明确报错退出码非零，列出冲突端口

## 输出契约

- `push` 打印每个传输文件的 远端路径 字节摘要行，便于日志比对
- `run` 中设备输出不加前缀直接透传 stdout/stderr（实时流），结束打印 `[device-exit: <code>]` 摘要行
- 所有目标的帮助描述注册进既有 `mk/help.mk` 机制（`make help` 可见）
