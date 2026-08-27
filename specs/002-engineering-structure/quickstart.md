# Quickstart: 工程结构与依赖库组织

**Branch**: `002-engineering-structure` | **Date**: 2026-08-26 | **Spec**: [spec.md](spec.md)

## Overview

本提案落地 C++ 网络库（001 架构）的 Bazel 6.5 工程骨架与第三方依赖组织，参考 graph_runtime 规范。产出可构建的 `//:netlib` 库目标与便捷构建入口。

## 快速开始

### 1. 前置要求

- Bazel 6.5（`.bazelversion` 已锁定）
- C++17 编译器（Clang/GCC）
- 网络可访问（依赖经 http_archive 拉取）

### 2. 初始化工程

```bash
# Platform setup: detect local OS/arch, generate git-ignored .user.bazelrc
./tools/platform_setup.sh
```

### 3. 构建与测试

```bash
# Automatic dependency fetching (netlib_setup idempotent) + full build
bazel build //...

# Smoke tests
bazel test //...

# Shared library target
bazel build //src/public:netlib_shared
```

### 4. 便利入口（Makefile）

```bash
make build      # = bazel build //...
make test       # = bazel test //...
make verify     # full flow: dependency resolution + build + test
make clean      # = bazel clean
make menu       # interactive help
```

### 5. 外部消费者示例

```bash
# examples/consumer_demo is a standalone workspace demonstrating local_repository dependencies
cd examples/consumer_demo && bazel build //... && bazel run //:demo
```

## 下一步

- 源码目录（src/http、src/tls 等）为占位结构，实际协议实现按 001 提案架构文档推进。
- 详见 [engineering-contract.md](contracts/engineering-contract.md) 的目录与目标契约。
