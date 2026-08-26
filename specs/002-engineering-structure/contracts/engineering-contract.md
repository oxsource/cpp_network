# 工程组织契约：构建结构与依赖引导

**Branch**: `002-engineering-structure` | **Date**: 2026-08-26 | **Spec**: [spec.md](../spec.md)

本契约定义工程组织的结构约束：目录布局、Bazel 目标、依赖引导、平台配置。供实现阶段（tasks）作为硬性验收依据。

## 目录布局契约

```text
<repo-root>/
├── WORKSPACE                # workspace(name = "cpp_network"); 调用 netlib_setup()
├── BUILD.bazel              # alias(name="netlib", actual="@cpp_network//src/public:netlib")
├── .bazelversion            # 6.5.0
├── .bazelrc                 # 基础配置 + 平台别名 + try-import .user.bazelrc
├── .gitignore               # 必须忽略 bazel-*、.user.bazelrc、C++ 产物
├── netlib_deps.bzl          # netlib_setup(): 幂等依赖引导
├── Makefile                 # 便利入口（转调 Bazel）
├── mk/                      # AOSP 风格模块化（rules/aliases/build/tests/help）
├── platforms/
│   ├── BUILD                # 五平台（config_setting + platform 成对）
│   └── platforms.bzl        # config_setting_and_platform / netlib_select 宏
├── tools/
│   └── platform_setup.sh    # 检测主机 → 生成 .user.bazelrc
├── third_party/
│   ├── libcurl/{BUILD.bazel, libcurl.bzl}
│   ├── openssl/{BUILD.bazel, openssl.bzl}
│   ├── googletest/BUILD.bazel
│   └── bazel_skylib/BUILD.bazel
├── src/
│   ├── http/BUILD.bazel
│   ├── websocket/BUILD.bazel
│   ├── tls/BUILD.bazel
│   ├── public/
│   │   ├── BUILD.bazel      # :netlib + :netlib_shared
│   │   └── include/netlib/{netlib.h, netlib_export.h}
│   ├── examples/BUILD.bazel
│   └── tests/{BUILD.bazel, smoke_test.cc}
└── examples/
    └── consumer_demo/       # 独立工作区（local_repository）
        ├── WORKSPACE
        ├── BUILD.bazel
        └── main.cc
```

## Bazel 目标契约

| 目标 | 类型 | 要求 |
|------|------|------|
| `//:netlib` | alias | 指向 `@cpp_network//src/public:netlib` |
| `@cpp_network//src/public:netlib` | cc_library | 静态库；hdrs 暴露 `public/include/netlib/` |
| `@cpp_network//src/public:netlib_shared` | cc_binary(linkshared) | 共享库；`-DNETLIB_SHARED_LIBRARY` + `NETLIB_API` 导出 |
| `@cpp_network//src/tls:tls` | cc_library | 经 `netlib_select` 选择 TLS 后端 |
| `@cpp_network//src/tests:smoke_test` | cc_test | 冒烟测试，`bazel test //...` 必过 |
| `@libcurl//:libcurl_openssl` | cc_library | OpenSSL 后端变体（USE_OPENSSL，全平台） |
| `//examples/consumer_demo:demo` | cc_binary | local_repository 消费验证 |

## 依赖引导契约（netlib_deps.bzl）

- `netlib_setup()` 幂等：每个 `http_archive` 用 `native.existing_rule(name)` 守卫。
- 每个依赖声明必须含：`name`、`sha256`、`urls`（或 `strip_prefix`）。
- 依赖清单（版本锁定）：bazel_skylib 1.6.1、curl ≥7.86、openssl 3.x、googletest 1.14.0。

## 平台契约

- `.bazelrc` 必须含：`--cxxopt=-std=c++17`、`--host_cxxopt=-std=c++17`、`--features=visibility=hidden`、`--enable_platform_specific_config`、`--test_output=errors`、五个 `build:<platform> --platforms=//platforms:<platform>` 别名、`try-import %workspace%/.user.bazelrc`。
- `platforms/platforms.bzl` 提供 `config_setting_and_platform(name, constraint_values)` 与 `netlib_select(map)`。
- `tools/platform_setup.sh`：Darwin→macos，Linux→linux，arm64/aarch64 映射；未知架构报错退出。

## 符号可见性契约

- 共享库构建：`-fvisibility=hidden` + `-DNETLIB_SHARED_LIBRARY`。
- `netlib_export.h` 定义 `NETLIB_API`（Windows dllimport/export；非 Windows `__attribute__((visibility("default")))`，镜像 graph_runtime `GRAPH_RUNTIME_API`）。
- 共享库符号白名单 = `NETLIB_API` 标注符号（`nm` 验证，SC-004）。

## 验收命令（实现阶段 gate）

```bash
./tools/platform_setup.sh                 # 生成 .user.bazelrc
bazel build //...                          # 零 error/warning
bazel test //...                           # 冒烟测试通过
bazel build //src/public:netlib_shared     # 共享库产物 + nm 检查符号
make build && make test && make verify     # 便利入口可用
# Android 平台解析（工具链环境具备时）：
bazel build --config=android_arm64 //src/tls:tls
```
