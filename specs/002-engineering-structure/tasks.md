# Tasks: 工程结构与依赖库组织

**Input**: Design documents from `/specs/002-engineering-structure/`

**Prerequisites**: plan.md (required), spec.md (required for user stories), research.md, data-model.md, contracts/

**Tests**: 本提案为工程搭建，无独立测试任务；冒烟测试本身是实现任务（T019）。不引入 TDD 流程。

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

## Path Conventions

- 工程根 = 仓库根（Bazel 工作区根）。参考 `contracts/engineering-contract.md` 的目录布局契约。

---

## Phase 1: Setup（工程初始化）

**Purpose**: 工作区基础文件与忽略规则

- [ ] T001 Create Bazel 工作区根文件 `WORKSPACE`（workspace name = "netlib"，预留 `netlib_setup()` 调用）
- [ ] T002 [P] Create `.bazelversion` 锁定 Bazel 6.5.0（内容 `6.5.0`）
- [ ] T003 [P] Create `.gitignore`（忽略 bazel-*、.user.bazelrc、C/C++ 产物、IDE/OS 文件，参考 graph_runtime）
- [ ] T004 [P] Create 根 `BUILD.bazel`（`alias(name="netlib", actual="//src/public:netlib")`）

---

## Phase 2: Foundational（阻塞性基础，US 前置）

**Purpose**: 平台定义、依赖引导、基础配置 —— 所有用户故事的共同前置

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

- [ ] T005 Create `platforms/platforms.bzl`（`config_setting_and_platform(name, constraint_values)` 与 `netlib_select(map)` 宏，镜像 graph_runtime）
- [ ] T006 Create `platforms/BUILD`（五平台：macos_arm64/macos_x86_64/linux_x86_64/linux_aarch64/android_arm64，config_setting+platform 成对）
- [ ] T007 Create `netlib_deps.bzl`（`netlib_setup()` 幂等引导：bazel_skylib 1.6.1、curl ≥7.86、openssl 3.x、boringssl 固定 commit、googletest 1.14.0，每依赖 sha256 + existing_rule 守卫）
- [ ] T008 [P] Create `.bazelrc`（C++17、-fvisibility=hidden、--enable_platform_specific_config、五平台 --config 别名、--test_output=errors、try-import .user.bazelrc）
- [ ] T009 Create `tools/platform_setup.sh`（检测 uname -s/-m → 生成 git-ignored `.user.bazelrc`；未知架构报错退出）
- [ ] T010 Update `WORKSPACE` 调用 `netlib_setup()`（T001 预留处）

**Checkpoint**: 平台 + 依赖基础就绪，用户故事可开始

---

## Phase 3: User Story 1 - 可构建的 Bazel 工程骨架 (Priority: P1) 🎯 MVP

**Goal**: 源码目录结构 + 公共库目标（//:netlib 静态+共享）+ 冒烟测试，`bazel build //...` 与 `bazel test //...` 成功

**Independent Test**: 运行 `./tools/platform_setup.sh` 后 `bazel build //...` 零 error/warning 产出 `//:netlib`；`bazel test //...` 冒烟测试通过；`bazel build //src/public:netlib_shared` 产出共享库且 `nm` 仅见 `NETLIB_API` 符号

### Implementation for User Story 1

- [ ] T011 [P] [US1] Create `src/http/BUILD.bazel`（占位 cc_library 目标，参考 contracts 目录契约）
- [ ] T012 [P] [US1] Create `src/websocket/BUILD.bazel`（占位 cc_library 目标）
- [ ] T013 [P] [US1] Create `src/public/include/netlib/netlib_export.h`（`NETLIB_API` 导出宏，镜像 graph_runtime `GRAPH_RUNTIME_API`）
- [ ] T014 [P] [US1] Create `src/public/include/netlib/netlib.h`（umbrella 占位头，include netlib_export.h）
- [ ] T015 [US1] Create `src/public/BUILD.bazel`（`:netlib` cc_library 静态 + `:netlib_shared` cc_binary(linkshared, linkstatic) 共享，共享用 -DNETLIB_SHARED_LIBRARY，depends T013/T014）
- [ ] T016 [P] [US1] Create `src/tests/smoke_test.cc`（冒烟测试：验证 netlib_export.h 可编译 / NETLIB_API 存在）
- [ ] T017 [US1] Create `src/tests/BUILD.bazel`（`:smoke_test` cc_test，依赖 googletest，depends T016）
- [ ] T018 [P] [US1] Create `src/examples/BUILD.bazel`（占位示例 cc_binary 目标）

**Checkpoint**: 工程骨架可构建可测 —— US1 MVP 达成

---

## Phase 4: User Story 2 - third_party 依赖库组织 (Priority: P1)

**Goal**: libcurl/OpenSSL/BoringSSL/googletest/bazel_skylib 独立封装，host=OpenSSL、android=BoringSSL 双后端变体

**Independent Test**: 干净缓存 `bazel build //...` 自动拉取锁定版本编译成功；`netlib_deps.bzl` 每依赖含 sha256 + existing_rule 幂等；`bazel build --config=android_arm64 //src/tls:tls` 解析 BoringSSL 分支

### Implementation for User Story 2

- [ ] T019 [P] [US2] Create `third_party/bazel_skylib/BUILD.bazel`（re-export @bazel_skylib 目标）
- [ ] T020 [P] [US2] Create `third_party/googletest/BUILD.bazel`（re-export gtest / gtest_main）
- [ ] T021 [P] [US2] Create `third_party/openssl/openssl.bzl`（版本锁定 3.x LTS + sha256）
- [ ] T022 [US2] Create `third_party/openssl/BUILD.bazel`（封装 @openssl//:ssl + :crypto，depends T021）
- [ ] T023 [P] [US2] Create `third_party/boringssl/boringssl.bzl`（固定 commit + sha256）
- [ ] T024 [US2] Create `third_party/boringssl/BUILD.bazel`（re-export @boringssl//:ssl + :crypto，depends T023）
- [ ] T025 [P] [US2] Create `third_party/libcurl/libcurl.bzl`（版本锁定 ≥7.86 + sha256）
- [ ] T026 [US2] Create `third_party/libcurl/BUILD.bazel`（`:libcurl_openssl` host 变体 USE_OPENSSL + `:libcurl_boringssl` android 变体，depends T025/T022/T024）
- [ ] T027 [US2] Create `src/tls/BUILD.bazel`（`//src/tls:tls` 经 `netlib_select` 选择 libcurl_openssl+openssl / libcurl_boringssl+boringssl，depends T026/T006）

**Checkpoint**: third_party 组织完成，TLS 后端平台选择可用

---

## Phase 5: User Story 3 - 跨平台配置验证 (Priority: P2)

**Goal**: 五平台经 `--config=<platform>` 可解析工具链；平台脚本正确生成 .user.bazelrc

**Independent Test**: `./tools/platform_setup.sh` 生成正确 `.user.bazelrc`；host 平台 `bazel build --config=<host> //...` 成功；五平台定义检查成对

### Implementation for User Story 3

- [ ] T028 [US3] 验证并修正 `tools/platform_setup.sh`（本机 host 平台实际运行，确认 `.user.bazelrc` 内容正确，depends T009）
- [ ] T029 [US3] 验证 host 平台构建：`./tools/platform_setup.sh && bazel build --config=<host_platform> //...`（确认零 error，depends T010/T027）
- [ ] T030 [US3] 验证五平台定义成对完整性（对照 contracts 平台契约检查 platforms/BUILD，depends T006）

**Checkpoint**: 跨平台配置验证通过

---

## Phase 6: User Story 4 - 便利构建入口 (Priority: P3)

**Goal**: Makefile + mk/ 模块化，build/test/verify/clean/menu 目标

**Independent Test**: `make verify` 触发依赖解析+构建+测试并 exit 0；`make build`/`make test`/`make clean` 各自成功

### Implementation for User Story 4

- [ ] T031 [P] [US4] Create `mk/rules.mk`（AOSP 风格模块注册宏：register_module/register_target/register_alias，镜像 graph_runtime）
- [ ] T032 [P] [US4] Create `mk/aliases.mk`（build/test/verify/clean 别名 → bazel 命令）
- [ ] T033 [P] [US4] Create `mk/help.mk`（help/menu 目标）
- [ ] T034 [US4] Create `Makefile`（include mk/*.mk，depends T031/T032/T033）
- [ ] T035 [US4] 验证 `make build` / `make test` / `make verify` / `make clean` 全部成功（depends T034）

**Checkpoint**: 便利入口可用

---

## Phase 7: Polish & Cross-Cutting Concerns

**Purpose**: 外部消费者示例 + 全流程验证

- [ ] T036 [P] Create `examples/consumer_demo/WORKSPACE`（local_repository 指向仓库根，独立工作区）
- [ ] T037 [P] Create `examples/consumer_demo/BUILD.bazel`（`:demo` cc_binary，deps @netlib//:netlib）
- [ ] T038 [P] Create `examples/consumer_demo/main.cc`（调用 netlib.h 的简单 demo，验证外部消费）
- [ ] T039 Update 主工作区 `.bazelignore`（排除 examples/consumer_demo，避免主工作区误解析）
- [ ] T040 [P] 验证 consumer_demo：`cd examples/consumer_demo && bazel build //...` 成功（depends T036/T037/T038）
- [ ] T041 全流程验证：`./tools/platform_setup.sh && make verify && nm 检查共享库符号仅 NETLIB_API`（depends 全部前置）

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: 无依赖，可立即开始
- **Foundational (Phase 2)**: 依赖 Setup — BLOCKS 所有用户故事
- **User Stories (Phase 3+)**: 依赖 Foundational；US1→US2→US3→US4 可部分并行
- **Polish (Final Phase)**: 依赖所有用户故事完成

### User Story Dependencies

- **US1 (P1)**: 依赖 Foundational（平台宏+依赖引导）；不依赖其他故事
- **US2 (P1)**: 依赖 Foundational（netlib_deps.bzl 拉取依赖）；可与 US1 并行（不同目录）
- **US3 (P2)**: 依赖 US1（构建验证）+ US2（TLS 后端）；验证性任务
- **US4 (P3)**: 依赖 US1 构建可用；与 US2/US3 可并行

### Within Each User Story

- 库目标依赖头文件（如 T015 依赖 T013/T014）；被依赖任务先完成
- 验证任务依赖实现任务

### Parallel Opportunities

- Setup T002/T003/T004 标记 [P] 可并行
- Foundational T008 [P] 与 T005/T006/T007 可并行
- US1 内 T011/T012/T013/T014/T016/T018 全部 [P] 可并行
- US2 内 T019-T025 大部分 [P] 可并行
- US4 内 T031/T032/T033 [P] 可并行
- US1 与 US2 不同目录可并行实施

---

## Parallel Example: User Story 1

```bash
# Launch all placeholder/header tasks together:
Task: "Create src/http/BUILD.bazel placeholder"
Task: "Create src/websocket/BUILD.bazel placeholder"
Task: "Create netlib_export.h"
Task: "Create netlib.h umbrella header"
Task: "Create smoke_test.cc"
Task: "Create src/examples/BUILD.bazel placeholder"

# Then (depends on netlib.h/netlib_export.h):
Task: "Create src/public/BUILD.bazel with :netlib + :netlib_shared"
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. 完成 Phase 1 + Phase 2（Setup + Foundational）
2. 完成 Phase 3（US1）：占位目录 + netlib 头 + 公共库目标 + 冒烟测试
3. **STOP and VALIDATE**: `./tools/platform_setup.sh && bazel build //... && bazel test //...`
4. 成功即交付 MVP（可构建可测的工程骨架）

### Incremental Delivery

1. Setup + Foundational → 基础就绪
2. US1 → 骨架可构建（MVP）
3. US2 → third_party 依赖组织（TLS 后端）
4. US3 → 跨平台验证
5. US4 → 便利入口
6. Polish → consumer_demo + 全流程验证

### Parallel Team Strategy

1. 团队共同完成 Setup + Foundational
2. Foundational 后：
   - 开发者 A: US1（骨架/公共库/冒烟）
   - 开发者 B: US2（third_party/TLS 后端）
3. US3/US4 复用验证与入口，可并行

---

## Notes

- [P] tasks = different files, no dependencies
- [Story] label maps task to specific user story for traceability
- 全部任务产出物在仓库根（Bazel 工作区），参考 `contracts/engineering-contract.md`
- 每个 checkpoint 可独立验证 story 完成
- 验收命令见 `contracts/engineering-contract.md` 的"验收命令"一节
- Commit after each task or logical group
- 避免：同文件冲突、破坏独立性的跨 story 依赖
