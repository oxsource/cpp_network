# Bazel Workspace & Platform Definitions Design

**Branch**: `001-cpp-network-library` | **Date**: 2026-08-26

**对应需求**: FR-014（Bazel 6.5 构建 macOS/Linux/Android）、FR-016（平台无关公共 API）、FR-003/FR-021（跨平台 TLS 后端选型）

**参考**: graph_runtime 的 `platforms/platforms.bzl` + `.bazelrc` `--config` 平台别名机制

## Overview

设计 Bazel 工作区布局、平台定义与构建时选择机制，使得：
1. 一个工作区支持 macOS (x86_64/arm64)、Linux (x86_64/aarch64)、Android (arm64)；
2. 平台相关符号隐藏与导出宏（`NETLIB_API`）机制可复用；
3. TLS 后端统一为 OpenSSL（全平台，无需 select 分支，见 tls-backend-selection.md）。

## 工作区布局

```text
WORKSPACE              # workspace(name = "netlib")
BUILD.bazel            # 根 BUILD：alias //:netlib -> //src/framework/public:netlib
.bazelversion          # 6.5.0
.bazelrc               # 项目级配置（提交）
netlib_deps.bzl        # 外部依赖引导宏（幂等）
platforms/
├── BUILD              # 平台定义
└── platforms.bzl      # config_setting_and_platform 宏
third_party/
├── libcurl/BUILD.bazel        # libcurl 依赖封装（全平台 OpenSSL 后端）
├── openssl/BUILD.bazel        # OpenSSL 依赖封装（全平台 TLS）
└── googletest/BUILD.bazel     # 测试依赖封装
src/
├── core/                      # 协议无关异步基础
├── http/                      # libcurl 封装
├── tls/                       # TLS 配置映射
├── public/include/netlib/     # 公共 API 头
├── examples/
└── tests/
tools/
└── platform_setup.sh          # 主机平台检测，生成 .user.bazelrc
```

## 平台定义（platforms/platforms.bzl）

复用 graph_runtime 的宏模式：

```python
# platforms/platforms.bzl
def config_setting_and_platform(name, constraint_values, parents=None):
    native.config_setting(
        name = name + "_setting",
        constraint_values = constraint_values,
    )
    native.platform(
        name = name,
        constraint_values = constraint_values,
        parents = parents,
    )

def netlib_select(select_map):
    return select(select_map)
```

`platforms/BUILD`：

```python
load("//platforms:platforms.bzl", "config_setting_and_platform")

config_setting_and_platform(
    name = "macos_arm64",
    constraint_values = [
        "@platforms//os:macos",
        "@platforms//cpu:aarch64",
    ],
)
config_setting_and_platform(
    name = "macos_x86_64",
    constraint_values = [
        "@platforms//os:macos",
        "@platforms//cpu:x86_64",
    ],
)
config_setting_and_platform(
    name = "linux_x86_64",
    constraint_values = [
        "@platforms//os:linux",
        "@platforms//cpu:x86_64",
    ],
)
config_setting_and_platform(
    name = "linux_aarch64",
    constraint_values = [
        "@platforms//os:linux",
        "@platforms//cpu:aarch64",
    ],
)
config_setting_and_platform(
    name = "android_arm64",
    constraint_values = [
        "@platforms//os:android",
        "@platforms//cpu:aarch64",
    ],
)
```

## .bazelrc 平台别名

```text
build --cxxopt=-std=c++17
build --host_cxxopt=-std=c++17
build --features=visibility=hidden
build --enable_platform_specific_config
build --test_output=errors

build:macos_arm64   --platforms=//platforms:macos_arm64
build:macos_x86_64  --platforms=//platforms:macos_x86_64
build:linux_x86_64  --platforms=//platforms:linux_x86_64
build:linux_aarch64 --platforms=//platforms:linux_aarch64
build:android_arm64 --platforms=//platforms:android_arm64

try-import %workspace%/user.bazelrc
```

`tools/platform_setup.sh`：检测 `uname -s` + `uname -m`，写入 `user.bazelrc` 的 `build --config=<platform>` 行（git-ignored），与 graph_runtime 一致。

## TLS 后端设计（全平台 OpenSSL，无 select）

TLS 后端统一为 OpenSSL（全平台 host + Android），`src/tls/BUILD.bazel` 无需平台 select：

```python
cc_library(
    name = "tls",
    srcs = ["tls_config.cc"],
    hdrs = ["tls_config.h"],
    deps = [
        "@openssl//:openssl",
        "@libcurl//:libcurl_openssl",
    ],
    visibility = ["//visibility:public"],
)
```

要点：
- 全平台统一 OpenSSL（用户决策，2026-08-26）；`netlib_select` 宏保留供其他条件依赖使用，但 TLS 不再依赖平台分支；
- libcurl 以 OpenSSL 后端编译（见 `host-openssl-build.md`；`android-boringssl-build.md` 已废弃）；
- 公共 API（`src/public/include/netlib/`）绝不出现 TLS 后端类型，保证 FR-016。

## 导出宏（netlib_export.h）

镜像 graph_runtime `graph_runtime_export.h`：

```cpp
#if defined(_WIN32)
  #if defined(NETLIB_SHARED_LIBRARY)
    #define NETLIB_API __declspec(dllexport)
  #else
    #define NETLIB_API __declspec(dllimport)
  #endif
#else
  #if defined(NETLIB_SHARED_LIBRARY)
    #define NETLIB_API __attribute__((visibility("default")))
  #else
    #define NETLIB_API
  #endif
#endif
```

## 依赖引导（netlib_deps.bzl）

```python
def netlib_setup():
    if native.existing_rule("libcurl"):
        return
    native.http_archive(name = "libcurl", ...)
    # 同法处理 openssl / googletest / bazel_skylib
```

幂等守卫 `native.existing_rule(...)` 避免重复定义。

## 边界与约束

- 本设计仅覆盖**构建系统与平台选择机制**，不含具体模块实现（见各模块设计文档）。
- iOS/Windows 平台明确 out of scope（spec Assumptions）。
- Bazel 版本锁定 6.5.0（`.bazelversion`）。

## 评审要点

1. `select()` 分支是否覆盖全部 5 个目标平台 + `//conditions:default` 兜底？
2. TLS 后端选择是否对公共 API 完全透明（无类型泄漏）？
3. `user.bazelrc` 是否被 `.gitignore` 忽略，避免平台配置污染提交？
4. 共享库导出宏（NETLIB_API）与 `-fvisibility=hidden` 是否一致？
