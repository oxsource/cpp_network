# Bazel Workspace & Platform Definitions Design

**Branch**: `002-engineering-structure`（落地）/ `003-http-implementation`（核对） | **Date**: 2026-08-26

> **状态（2026-08-26，实现核对；2026-08-27 更名核对）**：平台机制已按本文落地并验证（macOS arm64）。与原设计的差异：workspace 名为 `cpp_network`（001 初稿名为 `netlib`，启用前已统一更名）；根 BUILD 为 `alias //:cpp_network -> //src/public:cpp_network`；公共头在 `src/public/include/http/`；导出宏 `CPP_NETWORK_HTTP_EXPORT` / `CPP_NETWORK_HTTP_SHARED_LIBRARY`（http/export.h）；`.bazelrc` 无全局 `--features=visibility=hidden`（符号隐藏经各目标 copts/属性控制）；本地覆盖文件为 `.user.bazelrc`；依赖引导脚本为 `cpp_network_deps.bzl`（bazel_skylib/googletest 等经其注册于 third_party/）；`src/core/` 不存在。

**对应需求**: FR-014（Bazel 6.5 构建 macOS/Linux/Android）、FR-016（平台无关公共 API）、FR-003/FR-021（跨平台 TLS 后端选型）

**参考**: graph_runtime 的 `platforms/platforms.bzl` + `.bazelrc` `--config` 平台别名机制

## Overview

设计 Bazel 工作区布局、平台定义与构建时选择机制，使得：
1. 一个工作区支持 macOS (x86_64/arm64)、Linux (x86_64/aarch64)、Android (arm64)；
2. 平台相关符号隐藏与导出宏（`CPP_NETWORK_HTTP_EXPORT`）机制可复用；
3. TLS 后端统一为 OpenSSL（全平台，无需 select 分支，见 tls-backend-selection.md）。

## 工作区布局

```text
WORKSPACE              # workspace(name = "cpp_network")
BUILD.bazel            # Root BUILD: alias //:cpp_network -> //src/public:cpp_network
.bazelversion          # 6.5.0
.bazelrc               # Project-level config (committed)
platforms/
├── BUILD              # Platform definitions
└── platforms.bzl      # config_setting_and_platform macro + platform_select
third_party/
├── libcurl/           # Placeholder comment (currently links system -lcurl, see host-openssl-build.md)
├── openssl/           # Placeholder comment
├── bazel_skylib/      # Real dependencies
├── googletest/        # Test dependency wrapper
src/
├── http/              # HTTP implementation (engine + curl_mapping)
├── tls/               # Tls validation (tls.cc)
├── websocket/         # v2 placeholder
├── public/include/http/  # Public API headers (http_ prefixed)
└── tests/
examples/
└── consumer_demo/ http_demo/   # Standalone workspace consumption examples
tools/
└── platform_setup.sh          # Host platform detection, generates .user.bazelrc
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

def platform_select(select_map):
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

实际提交的 `.bazelrc`（与原设计差异：无全局 visibility=hidden；本地覆盖为 `.user.bazelrc`）：

```text
build --cxxopt=-std=c++17
build --host_cxxopt=-std=c++17
build --enable_platform_specific_config
build --test_output=errors

build:macos_arm64   --platforms=//platforms:macos_arm64
build:macos_x86_64  --platforms=//platforms:macos_x86_64
build:linux_x86_64  --platforms=//platforms:linux_x86_64
build:linux_aarch64 --platforms=//platforms:linux_aarch64
build:android_arm64 --platforms=//platforms:android_arm64

try-import %workspace%/.user.bazelrc
```

`tools/platform_setup.sh`：检测 `uname -s` + `uname -m`，写入 `.user.bazelrc` 的 `build --config=<platform>` 行（git-ignored），与 graph_runtime 一致。

## TLS 后端设计（全平台 OpenSSL，无 select）

TLS 后端统一为 OpenSSL（全平台 host + Android）。**当前落地**：`src/tls/BUILD.bazel` 仅依赖公共头（校验逻辑），CURLOPT 映射在 `src/http/detail/curl_mapping.cc`，libcurl 经系统库 `-lcurl` 链接：

```python
# src/tls/BUILD.bazel (as implemented)
cc_library(
    name = "tls",
    srcs = ["tls.cc"],
    deps = ["@cpp_network//src/public:public_headers"],
)

# engine/client targets in src/http/BUILD.bazel carry linkopts = ["-lcurl"]
```

要点：
- 全平台统一 OpenSSL（用户决策，2026-08-26）；`platform_select` 宏保留供其他条件依赖使用，但 TLS 不再依赖平台分支；
- 源码构建 libcurl/OpenSSL（`host-openssl-build.md` 方案 A）为后续任务；`android-boringssl-build.md` 已废弃；
- 公共 API（`src/public/include/http/`）绝不出现 TLS 后端类型，保证 FR-016。

## 导出宏（http/export.h）

实际实现（src/public/include/http/export.h，模式同 graph_runtime）：

```cpp
#if defined(_WIN32)
  #if defined(CPP_NETWORK_HTTP_SHARED_LIBRARY)
    #define CPP_NETWORK_HTTP_EXPORT __declspec(dllexport)
  #else
    #define CPP_NETWORK_HTTP_EXPORT __declspec(dllimport)
  #endif
#else
  #if defined(CPP_NETWORK_HTTP_SHARED_LIBRARY)
    #define CPP_NETWORK_HTTP_EXPORT __attribute__((visibility("default")))
  #else
    #define CPP_NETWORK_HTTP_EXPORT
  #endif
#endif
```

静态消费时宏为空；`cpp_network_shared` 目标以 `defines = ["CPP_NETWORK_HTTP_SHARED_LIBRARY"]` 构建。

## 边界与约束

- 本设计仅覆盖**构建系统与平台选择机制**，不含具体模块实现（见各模块设计文档）。
- iOS/Windows 平台明确 out of scope（spec Assumptions）。
- Bazel 版本锁定 6.5.0（`.bazelversion`）。

## 评审要点

1. `select()` 分支是否覆盖全部 5 个目标平台 + `//conditions:default` 兜底？
2. TLS 后端选择是否对公共 API 完全透明（无类型泄漏）？
3. `.user.bazelrc` 是否被 `.gitignore` 忽略，避免平台配置污染提交？（已满足）
4. 共享库导出宏（CPP_NETWORK_HTTP_EXPORT）与 `-fvisibility=hidden` 是否一致？（当前无全局 hidden flag，留共享库阶段统一处理）
