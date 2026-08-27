# Implementation Plan: 工程结构与依赖库组织

**Branch**: `002-engineering-structure` | **Date**: 2026-08-26 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `/specs/002-engineering-structure/spec.md`

## Summary

依据 001 提案架构（同步 API + libcurl 引擎 + Bazel select TLS），搭建完整的 Bazel 6.5 工程骨架与第三方依赖组织，参考 graph_runtime 规范：`netlib_deps.bzl` 依赖引导、`platforms/` 平台定义、`.bazelrc` 平台别名、`tools/platform_setup.sh`、Makefile + mk/ 模块化、`third_party/` 封装、`examples/consumer_demo` 消费者示例。产出可构建的 `//:netlib` 库目标（静态+共享）与冒烟测试。

## Technical Context

**Language/Version**: C++17（`.bazelrc` `--cxxopt=-std=c++17`）

**Primary Dependencies**: libcurl ≥7.86（协议引擎）、OpenSSL 3.x LTS（全平台 TLS 后端）、Google Test 1.14.x（测试）、bazel_skylib 1.6.x（构建辅助）。均经 Bazel `http_archive` 拉取，版本+sha256 锁定。

**Storage**: N/A — 工程骨架，无持久存储。

**Testing**: Google Test 冒烟测试（`src/tests`），验证工程结构可测。

**Target Platform**: macOS (x86_64, arm64), Linux (x86_64, aarch64), Android (arm64)

**Project Type**: C++ 库工程（Bazel 工作区）+ 独立消费者示例工作区

**Performance Goals**: 干净构建零 warning；二次构建缓存命中 <1min（SC-002）。

**Constraints**: Bazel 6.5.0 锁定、Google C++ Style Guide、graph_runtime 工程组织规范、依赖版本锁定可复现。

**Scale/Scope**: 工程骨架 + 依赖组织 + 平台配置 + 便利构建入口；不含协议实现代码（001 提案仅架构设计）。

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

Constitution 仍为模板，无定义原则，无违规可评估。Gate: PASS。

## Project Structure

### Documentation (this feature)

```text
specs/002-engineering-structure/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output
└── tasks.md             # Phase 2 output
```

### Source Code (repository root)

```text
WORKSPACE                  # workspace(name = "cpp_network") + netlib_setup()
BUILD.bazel                # root BUILD: alias //:netlib -> @cpp_network//src/public:netlib
.bazelversion              # 6.5.0
.bazelrc                   # base config + platform aliases
.gitignore                 # Bazel/C++/IDE/OS ignores
netlib_deps.bzl            # dependency bootstrap macro (idempotent)
Makefile                   # convenience build entry
mk/
├── rules.mk               # module registration macros (AOSP style)
├── aliases.mk             # build/test/verify/clean
├── build.mk
├── tests.mk
└── help.mk
platforms/
├── BUILD                  # five platform definitions
└── platforms.bzl          # config_setting_and_platform / netlib_select macros
tools/
└── platform_setup.sh      # host platform detection → generate .user.bazelrc
third_party/
├── libcurl/BUILD.bazel    # :libcurl_openssl (USE_OPENSSL, all platforms)
├── libcurl/libcurl.bzl    # version pinning
├── openssl/BUILD.bazel    # @openssl//:ssl, :crypto
├── openssl/openssl.bzl    # version pinning
├── googletest/BUILD.bazel # re-export gtest
└── bazel_skylib/BUILD.bazel
src/
├── http/BUILD.bazel        # empty/placeholder cc_library (structure in place)
├── websocket/BUILD.bazel   # empty/placeholder cc_library
├── tls/BUILD.bazel         # tls_config placeholder + netlib_select TLS backend
├── public/
│   ├── BUILD.bazel         # :netlib (static) + :netlib_shared (linkshared)
│   └── include/netlib/
│       ├── netlib.h        # umbrella header (placeholder)
│       └── netlib_export.h # NETLIB_API export macro
├── examples/
│   └── BUILD.bazel         # example binary (placeholder)
└── tests/
    ├── BUILD.bazel         # smoke test cc_test
    └── smoke_test.cc       # structure-validation smoke test
examples/
└── consumer_demo/          # standalone consumer workspace (local_repository)
    ├── WORKSPACE
    ├── BUILD.bazel
    └── main.cc
```

**Structure Decision**: 单主工作区（仓库根）+ 独立消费者 demo 工作区（`examples/consumer_demo`，经 `.bazelignore` 从主工作区排除），完全镜像 graph_runtime 组织。源码目录按 001 提案结构就位（占位 cc_library），公共库目标 `//:netlib`（静态 + `linkshared` 共享）。

## Complexity Tracking

N/A — Constitution 无违规，无需复杂度论证。
