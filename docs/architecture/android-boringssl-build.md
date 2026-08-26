# Android BoringSSL + libcurl Build Integration（⚠️ 已废弃）

> **DEPRECATED（2026-08-26）**：用户决策**全平台统一使用 OpenSSL**，Android 不再使用 BoringSSL。本文档仅保留作历史记录；Android 的 TLS 构建统一走 OpenSSL（见 [host-openssl-build.md](host-openssl-build.md) 及 [tls-backend-selection.md](tls-backend-selection.md) 修订版）。

**Branch**: `001-cpp-network-library` | **Date**: 2026-08-26（废弃标注）

**对应需求**: （原）FR-003（Android 用 BoringSSL）— 已修订为全平台 OpenSSL

**用户故事**: US2 (P1) — Platform-Specific TLS Adapter

**相关设计**: [tls-backend-selection.md](tls-backend-selection.md)、[bazel-platforms.md](bazel-platforms.md)、[tls-config.md](tls-config.md)

## Overview

（历史）原设计：Android (arm64, API 24+) 平台上 libcurl + BoringSSL 的构建集成方案，经 Bazel `select()` 触发，交付 `@libcurl//:libcurl_boringssl` 目标。**已被全平台 OpenSSL 方案取代，以下内容仅作参考。**

## 方案选择

### 方案 A：BoringSSL 源码 + libcurl 源码，Bazel 编译（推荐）

```text
WORKSPACE (netlib_deps.bzl)
 ├── @boringssl        # http_archive: BoringSSL 源码（CMake 项目）
 └── @libcurl          # http_archive: curl 源码
```

- BoringSSL 提供 Bazel 构建：官方 repo 自带 `BUILD`（`ssl`、`crypto` 目标），可直接 `http_archive` 引入。
- libcurl：curl 官方不提供 Bazel 构建，需：
  1. 用 `configure` 脚本生成 config（Android NDK 交叉编译环境），**或**
  2. 用 `gen/` 下已有的 `curl_config.h` + 手写 BUILD 封装（`curl_config.h` 按 Android 平台预生成）。
- 推荐路径：**预生成 Android 的 `curl_config.h` + 手写 `BUILD.bazel`** 封装 libcurl（方案 A2），避免在 Bazel 内跑 configure。

### 方案 B：Android NDK 预编译库 + cc_import

- 用 NDK `ndk-build`/CMake 预编译 libcurl+BoringSSL 静态库，经 `cc_import` 引入。
- 优点：构建快、不依赖在 Bazel 内交叉编译工具链。
- 缺点：版本升级需要外部重新编译流程；与 Bazel 平台工具链解耦导致 ABI 对齐风险（STL/API level）。

**决策**：采用**方案 A2**（源码 + 预生成 curl_config.h + 手写 BUILD）。理由：保持单一构建系统（Bazel）、版本可控、ABI 对齐（用 Bazel 的 android 工具链）。

## 目录结构（third_party）

```text
third_party/
├── boringssl/
│   ├── BUILD.bazel          # re-export @boringssl//:ssl, @boringssl//:crypto（官方 BUILD 已有）
│   └── boringssl.bzl        # 版本锁定 + 校验
├── libcurl/
│   ├── BUILD.bazel          # cc_library 封装 curl 源码（静态）
│   │   ├── :libcurl_openssl      # host：--with-openssl 语义
│   │   └── :libcurl_boringssl    # android：链接 @boringssl
│   ├── curl_config_android_arm64.h   # 预生成的 Android config（由官方 curl_config.h 裁剪）
│   ├── curl_config_host.h            # host config（见 host-openssl-build.md）
│   └── curl.bzl
```

## libcurl_boringssl BUILD 要点

```python
# third_party/libcurl/BUILD.bazel（Android 分支示意）
cc_library(
    name = "libcurl_boringssl",
    srcs = glob(["lib/**/*.c"]) + [
        "curl_config_android_arm64.h",
        "lib/curl_setup.h",
    ],
    hdrs = glob(["include/curl/*.h"]),
    defines = ["HAVE_CONFIG_H", "BUILDING_LIBCURL"],
    includes = ["include", "."],
    deps = [
        "@boringssl//:ssl",
        "@boringssl//:crypto",
        # 依赖 @platforms//os:android 工具链提供的系统 libc 等
    ],
    linkopts = select({
        "@platforms//os:android": ["-lz", "-llog"],
        "//conditions:default": [],
    }),
    visibility = ["//visibility:public"],
)
```

要点：
- **HAVE_CONFIG_H**：指向 `curl_config_android_arm64.h`，其内容需声明 `USE_OPENSSL`（BoringSSL 兼容 OpenSSL API 命名空间，libcurl 以 `USE_OPENSSL` 识别）并启用 `USE_SSL`。
- **BoringSSL 提供 OpenSSL 兼容 API**：BoringSSL 保留 OpenSSL 的 `SSL_*`/`X509_*` 符号，libcurl 用 `USE_OPENSSL` 宏路径即可编译，无需 `USE_BORINGSSL` 专用宏。
- **include 顺序**：`libcurl` 的 `include/` 在前，确保引用 `curl/curl.h` 为自家头。

## Android 平台配置

- 平台：`//platforms:android_arm64`（bazel-platforms.md 已定义）。
- 工具链：Bazel 内置 android toolchain 或 `rules_android_ndk`（android_ndk_repository）。若启用 `--config=android_arm64`，须在 `.bazelrc` 指定 NDK：
  ```text
  build:android_arm64 --crosstool_top=//external:android/crosstool
  ```
  （具体以 Bazel 6.5 的 Android 配置为准，见评审要点 2。）

## Android CA 处理（与 tls-config.md 联动）

- Android 系统 CA 位于设备 trust store（非文件系统标准路径），libcurl 默认**不会**自动加载。
- v1 方案：`android-boringssl-build.md` 提供**系统 CA bundle** 预置：
  - 构建时打包一份 Android CA bundle（由 NDK 系统镜像提取或 `curl_cacert.pem` 备份）；
  - 运行时 `TlsConfig` 默认若 `ca_certificates` 为空，Android 分支自动注入该 bundle（`CURLOPT_CAINFO`）。
- 该行为在 `tls-config.md` 明确文档化（Android 默认 CA 行为）。

## 验证矩阵

| 验证 | 命令 |
|------|------|
| Android arm64 构建 | `bazel build --config=android_arm64 //src/tls:tls` |
| 运行测试（需设备/模拟器或 qemu） | `bazel test --config=android_arm64 //src/tests/...` |
| host 构建不回归 | `bazel build --config=linux_x86_64 //src/tls:tls` |

## 边界与约束

- v1 仅支持 Android **arm64**（主流；armv7a/x86_64 留后续，宏需按 ABI 扩展 `curl_config_android_*.h`）。
- API level ≥ 24（spec Assumptions）。
- BoringSSL 不提供稳定 ABI/版本语义（跟随 master 或固定 tag）；必须在 `netlib_deps.bzl` 锁定 commit hash。

## 评审要点

1. BoringSSL 官方 BUILD 是否提供 `ssl`/`crypto` 两个 Bazel 目标可直接依赖？
2. `curl_config_android_arm64.h` 的生成方式（手工裁剪 vs 脚本）是否有可复现流程？
3. Bazel 6.5 的 Android NDK 工具链配置是否正确（crosstool / rules_android_ndk）？
4. Android 默认 CA bundle 的打包来源与注入机制是否明确？
5. ABI（arm64/API24/STL）与 Bazel android 工具链是否对齐？
