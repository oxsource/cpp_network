# Quickstart: Android HTTPS 支持与一键设备部署运行

**Branch**: `004-android-https-push-run`

## 一次性环境准备

1. 安装 Android 平台工具（含 `adb`）与 NDK r25+，导出环境变量：

```bash
export ANDROID_NDK_HOME=/path/to/android-ndk-r25c   # r25+（本机示例路径可不同）
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
make android_build
# 产出：cpp_network（arm64-v8a）+ 设备端可执行程序 device_e2e / http_demo
```

首次构建会自动下载并交叉编译 OpenSSL/curl 源码（锁定版本），耗时较长属预期；之后为增量构建。

## 一键部署运行

```bash
make android_push DEVICE=<serial>     # 可省略 DEVICE=…（仅一台设备时自动选择）
make android_run  DEVICE=<serial>
```

`run` 在设备上执行 e2e：默认**外网 HTTPS 场景**（example.com / httpbin.org，设备使用自身网络直连，无需与宿主同网段），实时回传输出并以设备端退出码结束。

成功样例结尾：

```text
[E1] PASS : HTTPS GET example.com (200 + body)
[E2] PASS : HEAD + case-insensitive header read
[E3] PASS : HTTPS POST JSON echo (httpbin)
PASS 3/3
[device-exit: 0]
```

失败时退出码 = 失败场景序号 + 1（场景清单见 contracts/device-test-contract.md；本地自签/mTLS 场景设置 `NETLIB_TEST_MODE=local` 并要求可达 fixtures）。

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
| `make android_push` 报未连接/未授权 | `adb devices` 核对状态；手机端允许 USB 调试 |
| 多台设备报候选列表 | 加 `DEVICE=<serial>` 重试 |
| run 报端口占用 | 不适用（外网模式无端口转发）；检查设备网络连通性 |
| run 报证书/连接错误 | `NETLIB_TEST_EXT_BASE=https://<可达地址>` 定向排查 |
| 构建期找不到 NDK | 确认 `ANDROID_NDK_HOME` 指向 r25+ 后重试 |

## 清理

```bash
make android_clean_device [DEVICE=<serial>]   # 移除设备端 /data/local/tmp/cpp_network/
bazel clean                           # 本机构建缓存（源码归档与 configure/make 产物经外置 repository/disk cache 自动恢复）
```
