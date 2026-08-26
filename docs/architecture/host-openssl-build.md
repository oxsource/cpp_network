# Host OpenSSL + libcurl Build Integration

**Branch**: `001-cpp-network-library` | **Date**: 2026-08-26

**对应需求**: FR-003（host 用 OpenSSL）、FR-014（macOS/Linux 构建）

**用户故事**: US2 (P1) — Platform-Specific TLS Adapter

**相关设计**: [tls-backend-selection.md](tls-backend-selection.md)、[bazel-platforms.md](bazel-platforms.md)（`android-boringssl-build.md` 已废弃）

## Overview

设计 host 平台（macOS x86_64/arm64, Linux x86_64/aarch64）上 libcurl + OpenSSL 的构建集成方案，交付 `@libcurl//:libcurl_openssl` 目标。host 分支由 Bazel `select()` 的 `//conditions:default` 触发。

## 方案选择

### 方案 A：OpenSSL 源码（http_archive）→ Bazel 编译

- OpenSSL 3.x LTS 提供 `build` 目录含手写 `BUILD`（`@openssl//:ssl`、`@openssl//:crypto`），或使用第三方 `bazel-openssl` 规则。
- 优点：单一构建系统、版本锁定、无系统依赖版本漂移。
- 缺点：OpenSSL 源码编译较慢；需要 `-fPIC`（静态库场景）。

### 方案 B：系统 OpenSSL（cc_library 链接系统库）

- macOS：Homebrew 路径（`/opt/homebrew/opt/openssl@3`）；Linux：系统 `libssl-dev`。
- 优点：构建快、无额外依赖下载。
- 缺点：版本不可控、跨机器不一致；不符合"构建可复现"目标。

**决策**：采用**方案 A**（源码 http_archive + Bazel 编译，OpenSSL 3.x LTS）。理由：可复现、版本锁定、与 Android 分支构建方式统一（都是源码构建）。

## 目录结构（third_party）

```text
third_party/
├── openssl/
│   ├── BUILD.bazel          # 封装 @openssl//:ssl, @openssl//:crypto + libcrypto
│   └── openssl.bzl          # 版本锁定（3.x LTS）+ sha256 校验
└── libcurl/
    ├── BUILD.bazel
    │   └── :libcurl_openssl      # 全平台目标（OpenSSL 后端，host + Android）
    ├── curl_config.h             # 全平台 config（含 USE_OPENSSL，按平台裁剪）
    └── curl.bzl
```

## libcurl_openssl BUILD 要点

```python
# third_party/libcurl/BUILD.bazel（host 分支示意）
cc_library(
    name = "libcurl_openssl",
    srcs = glob(["lib/**/*.c"]) + [
        "curl_config_host.h",
        "lib/curl_setup.h",
    ],
    hdrs = glob(["include/curl/*.h"]),
    defines = ["HAVE_CONFIG_H", "BUILDING_LIBCURL"],
    includes = ["include", "."],
    deps = [
        "@openssl//:ssl",
        "@openssl//:crypto",
    ],
    linkopts = [
        "-lz",
        # macOS/Linux 差异：由 select() 按平台补充
    ],
    visibility = ["//visibility:public"],
)
```

要点：
- **HAVE_CONFIG_H** → `curl_config_host.h`：声明 `USE_OPENSSL`、`USE_SSL`，启用 HTTPS 代理（`USE_HTTPSRR` 视版本）。
- **host 与 android 的 curl_config 分离**：`curl_config_host.h` 与 `curl_config_android_arm64.h` 各自独立（平台宏差异：如 `SIZEOF_*`、`HAVE_*` 平台函数）。
- **linkopts 平台差异**（macOS 需 `-framework Security` 非必需，OpenSSL 路径已足够）：
  ```python
  linkopts = select({
      "@platforms//os:macos": [],
      "@platforms//os:linux": ["-ldl", "-lpthread"],
      "//conditions:default": [],
  }),
  ```

## OpenSSL 版本与安全基线

- **锁定 OpenSSL 3.x LTS**（如 3.0.x/3.3.x），并在 `openssl.bzl` 固定 commit + sha256。
- OpenSSL 3.x 启用默认安全级别（`SECLEVEL=2`），满足 TLS 1.2+ 默认；如需调整由 `TlsConfig` 未来暴露（v1 不动）。
- 安全公告跟进由依赖版本升级流程覆盖（记录在 ADR）。

## 平台差异汇总

| 项 | macOS | Linux |
|----|-------|-------|
| OpenSSL 来源 | `@openssl//:ssl`（源码，同 Linux） | 同左 |
| 额外链接 | 无（`-lz` 由 curl 需要） | `-ldl`, `-lpthread` |
| 系统 CA | 系统信任库（libcurl 默认） | `/etc/ssl/certs`（libcurl 默认） |
| 符号可见性 | `-fvisibility=hidden` + `NETLIB_API` 导出 | 同左 |

## 验证矩阵

| 验证 | 命令 |
|------|------|
| macOS arm64 构建 | `bazel build --config=macos_arm64 //src/tls:tls` |
| macOS x86_64 构建 | `bazel build --config=macos_x86_64 //src/tls:tls` |
| Linux x86_64 构建 | `bazel build --config=linux_x86_64 //src/tls:tls` |
| Linux aarch64 构建 | `bazel build --config=linux_aarch64 //src/tls:tls` |
| Android 不回归 | `bazel build --config=android_arm64 //src/tls:tls` |

## 边界与约束

- host 全部平台共用同一 `curl_config_host.h`（各平台差异由 select() 的 linkopts/defines 收敛）。
- 不使用系统 OpenSSL（保证可复现性）；如需系统库加速见方案 B，但不在 v1。
- OpenSSL 3.x 为全平台 TLS 后端（host + Android）；Android 上的交叉编译（NDK 工具链）在 TLS 实现阶段确认。

## 评审要点

1. `curl_config_host.h` 是否与 android 版本分离（避免平台宏污染）？
2. OpenSSL 3.x LTS 的 commit/sha256 是否锁定（可复现）？
3. macOS/Linux 的 linkopts 差异是否经 select() 正确收敛？
4. 符号隐藏（-fvisibility=hidden）下 OpenSSL 符号是否不外泄到共享库？
