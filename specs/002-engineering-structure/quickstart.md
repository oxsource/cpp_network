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
# 平台设置：检测本机 OS/架构，生成 git-ignored 的 .user.bazelrc
./tools/platform_setup.sh
```

### 3. 构建与测试

```bash
# 依赖自动拉取（netlib_setup 幂等）+ 全量构建
bazel build //...

# 冒烟测试
bazel test //...

# 共享库目标
bazel build //src/public:netlib_shared
```

### 4. 便利入口（Makefile）

```bash
make build      # = bazel build //...
make test       # = bazel test //...
make verify     # 依赖解析 + 构建 + 测试全流程
make clean      # = bazel clean
make menu       # 交互式帮助
```

### 5. 外部消费者示例

```bash
# examples/consumer_demo 是独立工作区，演示 local_repository 依赖
cd examples/consumer_demo && bazel build //... && bazel run //:demo
```

## 下一步

- 源码目录（src/http、src/tls 等）为占位结构，实际协议实现按 001 提案架构文档推进。
- 详见 [engineering-contract.md](contracts/engineering-contract.md) 的目录与目标契约。
