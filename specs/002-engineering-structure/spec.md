# Feature Specification: 工程结构与依赖库组织

**Feature Branch**: `002-engineering-structure`

**Created**: 2026-08-26

**Status**: Draft

**Input**: User description: "依据001提案架构需求，设计实现工程机构及相关依赖库组织，参考graph_runtime的规范及组织要求"

## User Scenarios & Testing *(mandatory)*

### User Story 1 - 搭建可构建的 Bazel 工程骨架 (Priority: P1)

一个 C++ 开发者克隆仓库后，依据 001 提案的架构（src/http、src/websocket、src/tls、src/public/include/netlib、src/examples、src/tests），工程已具备完整的 Bazel 工作区结构：根 BUILD/WORKSPACE、平台定义、依赖引导宏。开发者执行一次平台设置脚本后，`bazel build //...` 能成功解析依赖并构建出静态/共享库目标。

**Why this priority**: 工程骨架是所有后续实现的地基。没有可解析的 Bazel 工作区，任何代码都无法编译验证。

**Independent Test**: 可以独立验证——运行平台设置脚本，然后 `bazel build //...` 成功，产出 `netlib` 库目标（静态与共享）。

**Acceptance Scenarios**:

1. **Given** 仓库刚克隆、无任何构建产物，**When** 开发者运行平台设置脚本后执行 `bazel build //...`，**Then** Bazel 成功解析全部依赖（无 missing dependency 错误）并构建出 `//:netlib` 目标。

2. **Given** 工程已初始化，**When** 开发者执行 `bazel test //...`，**Then** 至少存在一个可通过的冒烟测试目标（验证工程结构可测）。

3. **Given** 工程根目录，**When** 开发者运行 `bazel build //src/public:runtime_shared`（或等价共享库目标），**Then** 产出共享库文件（.so/.dylib），且仅导出 `NETLIB_API` 标注符号。

---

### User Story 2 - 依赖库的 third_party 组织 (Priority: P1)

工程需要封装 libcurl、OpenSSL、googletest 等外部依赖。开发者打开 `third_party/` 目录，能看到每个依赖有独立的封装目录（BUILD + 版本锁定 bzl），遵循 graph_runtime 的 `graph_runtime_deps.bzl` 引导模式。依赖通过 Bazel `http_archive` 拉取，版本与校验和锁定，幂等可复现。TLS 统一使用 OpenSSL（所有平台）。

**Why this priority**: 依赖组织决定跨平台构建的可复现性。TLS 后端（OpenSSL）的封装正确性依赖 third_party 组织。

**Independent Test**: 可以独立验证——执行 `bazel build` 时依赖自动解析成功；查看 `netlib_deps.bzl` 中各依赖带版本与 sha256。

**Acceptance Scenarios**:

1. **Given** 干净的构建缓存，**When** 开发者执行 `bazel build //...`，**Then** libcurl、openssl（host）、googletest 从锁定版本自动拉取并编译成功。

2. **Given** `netlib_deps.bzl`，**When** 开发者检查各依赖声明，**Then** 每个依赖含明确的版本/commit 与 sha256 校验，且用 `native.existing_rule` 做幂等守卫（重复 setup 不报错）。

3. **Given** 工程配置（含 Android 平台），**When** 开发者执行 `bazel build --config=android_arm64 //src/tls:tls`，**Then** OpenSSL 作为 TLS 后端被解析构建。

---

### User Story 3 - 跨平台配置与平台定义 (Priority: P2)

工程提供与 graph_runtime 一致的平台抽象：`platforms/` 目录定义 macos_arm64、macos_x86_64、linux_x86_64、linux_aarch64、android_arm64 五个平台；`.bazelrc` 提供 `--config=<platform>` 别名；平台设置脚本自动生成 `.user.bazelrc`。开发者无需了解 Bazel 平台细节即可按平台构建。

**Why this priority**: 跨平台是 001 提案核心约束（macOS/Linux/Android），平台抽象是工程化落地关键。

**Independent Test**: 可以独立验证——分别执行五个平台的 `bazel build --config=<platform> //...`，均可解析正确工具链。

**Acceptance Scenarios**:

1. **Given** 工程已初始化，**When** 开发者运行 `./tools/platform_setup.sh`，**Then** 生成 git-ignored 的 `.user.bazelrc`，包含正确的 `build --config=<本机平台>` 行。

2. **Given** `platforms/BUILD`，**When** 开发者检查平台定义，**Then** 五个平台均以 `config_setting` + `platform` 成对定义（复用 `config_setting_and_platform` 宏）。

3. **Given** `.bazelrc`，**When** 开发者查看，**Then** 包含 `--enable_platform_specific_config`、`-fvisibility=hidden`、C++17 等与 graph_runtime 一致的基础配置。

---

### User Story 4 - 便捷构建与验证入口 (Priority: P3)

工程提供开发者友好的构建入口（参考 graph_runtime 的 Makefile + mk/ 模块化组织）：`make build`、`make test`、`make verify`、`make clean`、`make menu` 等目标，内部调用 Bazel。新开发者无需记忆 Bazel 命令即可完成常见操作。

**Why this priority**: 提升开发体验与工程规范性，但属于便利层，非核心功能。

**Independent Test**: 可以独立验证——运行 `make verify` 依次触发依赖解析、构建、测试并全部成功退出（exit 0）。

**Acceptance Scenarios**:

1. **Given** 工程根目录，**When** 开发者运行 `make build`，**Then** 触发 `bazel build //...` 并成功。

2. **Given** 工程根目录，**When** 开发者运行 `make test`，**Then** 触发 `bazel test //...` 并全部通过。

3. **Given** 工程根目录，**When** 开发者运行 `make clean`，**Then** 触发 `bazel clean` 并成功。

---

### Edge Cases

- 什么情况会发生依赖解析失败？网络不可达导致 http_archive 拉取失败 → 提供清晰的错误提示与重试指引。
- 如何处理重复执行依赖 setup？`netlib_deps.bzl` 的 `existing_rule` 幂等守卫，重复调用不报错。
- 如何处理平台脚本在未知架构（如 riscv）上运行？脚本降级为报错并提示支持范围，不静默生成错误配置。
- 如何处理 `.user.bazelrc` 与提交的 `.bazelrc` 冲突？`.user.bazelrc` 以 try-import 低优先级覆盖，且被 gitignore。
- 如何处理共享库在 macOS 的符号可见性？`-fvisibility=hidden` + `NETLIB_API` 标注，仅导出公共符号。
- 如何处理 Android 工具链缺失？`--config=android_arm64` 构建时给出明确 NDK 配置指引，而非晦涩报错。

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: 工程 MUST 提供可被 Bazel 6.5 解析的完整工作区（根 BUILD/WORKSPACE、`.bazelversion` 锁定 6.5.0）。
- **FR-002**: 工程 MUST 提供依赖引导宏 `netlib_deps.bzl`，幂等拉取 libcurl、openssl、googletest、bazel_skylib，且每个依赖带版本锁定与 sha256 校验。
- **FR-003**: 工程 MUST 在 `platforms/` 下定义五个平台（macos_arm64/macos_x86_64/linux_x86_64/linux_aarch64/android_arm64），以 `config_setting` + `platform` 成对方式。
- **FR-004**: 工程 MUST 提供 `.bazelrc`，含 C++17、`-fvisibility=hidden`、`--enable_platform_specific_config`、`--config=<platform>` 别名、`--test_output=errors`，并通过 `try-import` 加载 `.user.bazelrc`。
- **FR-005**: 工程 MUST 提供平台设置脚本 `tools/platform_setup.sh`，自动检测主机 OS/架构并生成 `.user.bazelrc`；该文件 MUST 被 gitignore。
- **FR-006**: 工程 MUST 按 001 提案目录结构组织源码：`src/http`、`src/websocket`、`src/tls`、`src/public/include/netlib`、`src/examples`、`src/tests`，各目录有 BUILD 文件。
- **FR-007**: 工程 MUST 提供公共库目标 `//:netlib`（静态）与共享库目标（`linkshared`），共享库仅导出 `NETLIB_API` 符号。
- **FR-008**: 工程 MUST 在 `third_party/` 为每个外部依赖提供独立封装目录（BUILD + 版本锁定的 bzl），TLS 统一使用 OpenSSL（host 与 Android 平台）。
- **FR-009**: 工程 MUST 提供 `src/tests` 冒烟测试目标，验证工程骨架可测（至少一个通过的空测试/结构测试）。
- **FR-010**: 工程 MUST 提供便利构建入口（Makefile + mk/ 模块化，参考 graph_runtime），至少包含 build/test/verify/clean 目标。
- **FR-011**: 工程 MUST 提供 `.gitignore`，忽略 Bazel 产物（bazel-*、.user.bazelrc）、C/C++ 构建产物、IDE/OS 文件。
- **FR-012**: 工程 MUST 提供外部消费者示例（`examples/consumer_demo` 或等价），演示其他工程如何经 `local_repository` 依赖本库。

### Key Entities *(include if feature involves data)*

- **Bazel 工作区（WORKSPACE）**: 工程构建的根；声明 workspace 名 `netlib`，经 `netlib_setup()` 引导外部依赖。
- **平台定义（platforms/）**: 五个命名平台，每个为 `config_setting`+`platform` 对，供 `select()` 与 `--config` 使用。
- **依赖引导宏（netlib_deps.bzl）**: 幂等拉取外部依赖的单一入口，含版本与 sha256。
- **third_party 封装**: 每个外部依赖的独立封装（BUILD + bzl），隔离版本与平台差异。
- **公共 API 目标（//:netlib）**: 工程对外交付的库目标（静态+共享），对应 001 提案的 public/include/netlib 头。
- **便利构建入口（Makefile）**: 开发者友好的命令映射层，内部转调 Bazel。

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: 新克隆仓库的开发者按文档执行（平台脚本 + `bazel build //...`）在 30 分钟内成功产出 `//:netlib` 目标（含依赖拉取时间）。
- **SC-002**: `bazel build //...` 在干净缓存下零错误、零 warning 通过；二次构建因缓存命中在 1 分钟内完成。
- **SC-003**: 五个平台定义均能经 `--config=<platform>` 成功解析工具链（至少 host 平台实际构建通过，Android 平台解析通过）。
- **SC-004**: `bazel test //...` 冒烟测试全部通过；共享库目标链接成功且仅导出 `NETLIB_API` 符号（`nm` 检查）。
- **SC-005**: `make verify` 单条命令完成依赖解析 + 构建 + 测试全流程并 exit 0。
- **SC-006**: 依赖版本全部锁定（无浮动版本），两次干净构建产出的依赖版本一致（可复现）。

## Assumptions

- 参考 graph_runtime 的工程组织：`graph_runtime_deps.bzl` 引导、`platforms/platforms.bzl` 宏、`.bazelrc` 平台别名、`tools/platform_setup.sh`、Makefile + mk/ 模块化、`third_party/` 封装、`examples/consumer_demo` 消费者示例。
- 本提案落地工程骨架与依赖组织，不含协议实现代码（协议实现在 001 提案中为架构设计，本提案只搭结构，代码留后续实现阶段）。
- Bazel 版本锁定 6.5.0（`.bazelversion`）；host 平台 macOS/Linux 实际可构建，Android arm64 平台定义可解析（实际 NDK 构建依赖工具链环境，本提案保证平台配置与依赖解析正确）。
- 依赖版本默认值：libcurl ≥7.86（WebSocket 支持）、OpenSSL 3.x LTS（全平台 TLS）、googletest 1.14.x、bazel_skylib 1.6.x（与 graph_runtime 对齐）。
- 公共库命名 `netlib`（001 提案命名），导出宏 `NETLIB_API`。
- 工程根位于仓库根目录（Bazel 工作区根），沿用 graph_runtime 的"主工作区 + 独立消费者 demo 工作区"模式。