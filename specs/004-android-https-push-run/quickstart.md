# Quickstart: Android HTTPS 支持与一键设备部署运行

**Branch**: `004-android-https-push-run`

## 一次性环境准备

1. 安装 Android 平台工具（含 `adb`）与 NDK r26+，导出环境变量：

```bash
export ANDROID_NDK_HOME=/path/to/android-ndk-r26d   # r26+ 建议版本
export PATH="$PATH:/path/to/android-sdk/platform-tools"  # 提供 adb
```

2. 连接设备并确认可用：

```bash
adb devices -l        # 期望看到 state=device 的条目；unauthorized 需在手机上授权
```

3. 工具自检：

```bash
tools/platform_setup.sh    # 会提示 ANDROID_NDK_HOME 与 adb 是否就绪
```

## 构建 Android 产物

```bash
make build-android
# 产出：cpp_network（arm64-v8a）+ 设备端可执行程序 device_e2e / http_demo
```

首次构建会自动下载并交叉编译 OpenSSL/curl 源码（锁定版本），耗时较长属预期；之后为增量构建。

## 一键部署运行

```bash
make push DEVICE=<serial>     # 可省略 DEVICE=…（仅一台设备时自动选择）
make run  DEVICE=<serial>
```

`run` 将依次：启动宿主测试服务（HTTP :18080 / TLS :18443）→ 建立 `adb reverse` 端口反向转发 → 在设备上执行 e2e → 实时回传输出 → 以设备端退出码结束。

成功样例结尾：

```text
PASS 7/7
[device-exit: 0]
```

失败时退出码 = 失败场景序号 + 1（场景清单见 contracts/device-test-contract.md）。

## 应用开发者视角（零平台分支）

```cpp
// 同一份代码，host 与 android 行为一致：
cpp_network::http::Tls tls =
    cpp_network::http::Tls::Builder()
        .SetCaFile("/data/local/tmp/cpp_network/certs/ca_cert.pem") // 或 SetCaPem(内存文本)
        .Build();
cpp_network::http::Options opts;
opts.SetTls(tls);
auto client = cpp_network::http::Client::Create(opts);   // 无任何 #ifdef
```

> 注意：Android 上建立信任锚需显式注入 CA（系统信任库缺省不可用于本库 v1）；错误码语义与桌面平台一致。

## 故障排查

| 现象 | 处理 |
|------|------|
| `make push` 报未连接/未授权 | `adb devices` 核对状态；手机端允许 USB 调试 |
| 多台设备报候选列表 | 加 `DEVICE=<serial>` 重试 |
| run 报端口占用 | `PORTS="28080 28443"` 换端口，或释放占用进程 |
| 构建期找不到 NDK | 确认 `ANDROID_NDK_HOME` 指向 r26+ 后重试 |

## 清理

```bash
make clean-device [DEVICE=<serial>]   # 移除设备端 /data/local/tmp/cpp_network/
bazel clean                           # 本机构建缓存（含第三方源码编译产物）
```
