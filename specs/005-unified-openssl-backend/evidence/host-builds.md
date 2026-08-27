# 宿主构建证据（specs/005 Phase 4 / T010-T012）

日期：2026-08-27；执行机：Mac mini M4 (Darwin/arm64)

## T010 依赖审计
- `make deps_audit`：扫描 BUILD*/bzl 中 `-lcurl` 引用 → **0 命中**（FR-003 断言通过，rc=0）
- 覆盖范围：src/**、third_party/**、platforms/**、BUILD.bazel、WORKSPACE、cpp_network_deps.bzl

## T011 各配置构建验证（TLSCapabilityMatrix 行标注依据）

| 平台配置 | 验证等级 | 证据 |
|----------|----------|------|
| android_arm64 | runtime-verified | specs/004 + 本特性 android_verify 多轮真机全绿 |
| macos_arm64 | runtime-verified | Phase 2 取证：gtest 6/6、e2e 双模式全绿、demo 四段 200 |
| macos_x86_64 | **build-only** | `TLS_HOST_ARCH=x86_64` 经 `-arch x86_64` 交叉编译：`libcurl_la-altsvc.o` = **Mach-O x86_64**；bundle 目标构建 rc=0 |
| linux_x86_64 | analysis-only（executor pending） | `bazel build --config=linux_x86_64 --nobuild //src/public:cpp_network //src/tests:device_e2e` rc=0；无 Linux 执行机，编译/运行需后续 CI |
| linux_aarch64 | analysis-only（executor pending） | 同上，rc=0 |

> 执行机约束（诚实声明）：genrule 按"执行机架构+TLS_HOST_ARCH"产出对象。Linux 配置在本 Mac 上强行执行会得到错误架构产物——故仅做分析级并明示待补。

## T012 最小工具集探针（MODE=host 运行要求）

在剥离 PATH 的沙箱内运行 genrule 成功，所需工具收敛为：

- 编译器：系统 cc/clang（configure 自带探测）
- 归档器/索引器：macOS 强制 `/usr/bin/ar`、`/usr/bin/ranlib`（防 GNU-ar 抢占）
- make、grep/sed/coreutils（脚本工具链）
- perl（OpenSSL Configure 需要）
- **不需要**：pkg-config、libtool、autoconf/automake（curl 用发布包自带 configure）

清单已同步 quickstart 环境准备节。
