# Implementation Plan: 全平台统一 OpenSSL TLS 后端

**Branch**: `005-unified-openssl-backend` | **Date**: 2026-08-27 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `/specs/005-unified-openssl-backend/spec.md`

## Summary

将 specs/004 建立的「源码构建 OpenSSL 3.0.13 + libcurl 8.7.1 静态链接」从 Android 分支扩展为**全平台默认**：host（macOS arm64/x86_64、Linux x86_64/aarch64）通过泛化后的单一构建驱动以原生模式产出同一锁定版本的静态库，`src/http` 的链接选择收敛为无条件使用内置 bundle 并彻底移除系统 libcurl 路径；第三方符号在对象层隐藏（严格策略），共享形态产物仅导出公开 API。验证矩阵按平台标注"运行级实测 / 构建级验证"，ADR 同步第三次修订。

## Technical Context

**Language/Version**: C++17；Bazel 6.5.0；延续 004 的 genrule 构建驱动与 `cpp_network_deps.bzl` 单一事实源

**Primary Dependencies**: OpenSSL 3.0.13 LTS 与 curl 8.7.1（pin 不变）；无需新外部依赖（无 NDK 参与 host 分支）

**Storage**: N/A

**Testing**: 宿主全量 gtest（6 套件）+ device_e2e 双模式（macOS 运行级）；Linux 配置仅构建级验证并写入矩阵等级

**Target Platform**: macOS arm64（运行级验收基线）、macOS x86_64 / Linux x86_64 / Linux aarch64（构建级）、Android arm64（已验收，回归保护）；Windows 维持排除

**Project Type**: C++ 库（既有特性架构演进，非新增能力面）

**Performance Goals**: 构建：新增四个宿主配置在磁盘缓存命中下增量 <10s、冷构建 ≈40s/配置；产物体积增长不超出预期核算（记录于验证证据）

**Constraints**: 版本 pin 不得漂移；`device_e2e` 场景/退出码协议与测试资产向后兼容（FR-009）；共享库符号隐藏后不能破坏静态消费路径

**Scale/Scope**: 触及文件 ≤12 个、diff 约 ±250 行；不做 Windows，不改公共 API 语义

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

Constitution 为模板、无已定义原则，无违规可评估。Gate: PASS。

## Project Structure

### Documentation (this feature)

```text
specs/005-unified-openssl-backend/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
└── contracts/           # Phase 1 output
    └── symbol-visibility.md   # 导出符号契约与审计方法
```

### Source Code (workspace root: `cpp_network/`)

> 以下文件路径均相对仓库根的 **`cpp_network/`** Bazel 工作区（仓库根另含 `specs/`）。

```text
third_party/scripts/build_openssl.sh      # 由 build_openssl.sh 泛化：MODE=android|host，
                                      #   host 按 uname 选 Configure 目标与原生工具链
third_party/openssl/{host,android}/BUILD.bazel  # 目录更名（androidtls→tls→openssl 落定）；
                                      #   genrule 增加 host 变体目标（同目标名跨配置隔离）
src/http/BUILD.bazel                  # select 收敛：无条件依赖 //third_party/openssl/{host,android}:curl，
                                      #   删除 -lcurl 分支与 android 专属注入
.bazelrc                              # 移除 host 无关噪音项；android action_env 保留
tools/android_device.sh               # 注释更新（机制名不变）
docs/architecture/tls-config.md       # 平台差异表全行刷新（含 macOS BLOB 怪癖消失）
docs/architecture/tls-backend-selection.md  # 矩阵全面刷新 + ADR-003 v3 引用
docs/architecture/adr/adr-003-tls-buildtime-select.md  # 第三次修订段落
specs/005-*/quickstart.md             # 升级演练步骤（FR-004）
specs/004-android-https-push-run/research.md  # 追加 D14 交叉引用注记（不动历史文本）
AGENTS.md                             # SPECKIT 指向本 plan
```

**Structure Decision**: 构建资产保持单一定义点——脚本一份、genrule 一份、注入清单沿用；目录重命名与否在执行时依据 `bazel query` 影响面决定（最终落定为 `third_party/openssl/` 单包承载全平台）。

## Constitution Check (post-design)

Gate: PASS（同上，模板无可评估原则）。

## Complexity Tracking

N/A — 无违反简化原则的项。
