# Tasks: 全平台统一 OpenSSL TLS 后端

**Input**: Design documents from `/specs/005-unified-openssl-backend/`

**Prerequisites**: plan.md（技术路径）、spec.md（US1–US4）、research.md（D1–D5 决策）、data-model.md、contracts/symbol-visibility.md、quickstart.md

**Tests**: 场景验收即功能交付的一部分；无独立"可选测试"。运行级验证仅在 macOS arm64 执行，Linux 与 macOS x86_64 为构建级。

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2)
- Include exact file paths in descriptions

## Path Conventions

仓库为单一 Bazel workspace。构建资产在 `third_party/`，链接选择在 `src/http/BUILD.bazel`，共享形态产物目标为 `//src/public:cpp_network_shared`。

---

## Phase 1: Foundational (Blocking Prerequisites)

**Purpose**: 宿主模式构建能力与链接翻转的前置项；阻塞全部用户故事

- [ ] T001 Generalize third_party/scripts/build-android-tls.sh into a MODE-parameterized driver (MODE=android preserves current behavior byte-for-byte; MODE=host selects OpenSSL Configure target via uname mapping darwin64-arm64/darwin64-x86_64/linux-x86_64/linux-aarch64, drops cross flags/--host, keeps the identical trim set, exports AR=/usr/bin/ar & RANLIB=/usr/bin/ranlib on macOS, injects -fvisibility=hidden -fPIC) per research.md D1
- [ ] T002 Update third_party/androidtls/BUILD.bazel: genrule dispatches on config (android_arm64 -> MODE=android with ANDROID_NDK_HOME; macos/linux configs -> MODE=host) so the same target names build per-config outputs for all five platforms (data-model BuildArtifact.config 细化)
- [ ] T003 Verify host variant builds: `bazel build --config=macos_arm64 //third_party/androidtls:build_tls` produces Mach-O-format static archives + include/curl headers; record archive format check evidence (D1 防御性 ar 选择验证)

**Checkpoint**: 宿主源码构建能力就绪——后续翻转无基础设施风险

---

## Phase 2: User Story 1 - 宿主平台与 Android 行为完全一致 (Priority: P1) 🎯 MVP

**Goal**: macOS 默认走内置 bundle 并通过全部既有测试与场景，行为与 Android 逐项一致

**Independent Test**: 翻转后全量 gtest 6 套件 + device_e2e 双模式 + http_demo 全绿；S2/S3 断言文本不变

### Implementation for User Story 1

> **NOTE**: T004 是不可逆翻转提交的单粒度边界；回退手段 = git revert（research.md D5）

- [ ] T004 [US1] Converge src/http/BUILD.bazel link selection: unconditionally depend on //third_party/androidtls:android_curl (rename later if desired) for ALL configs, delete both `-lcurl` legacy branches, keep android action_env only where needed (.bazelrc untouched otherwise); FR-003 完成
- [ ] T005 [US1] macOS runtime regression evidence capture: run all six gtest suites, device_e2e dual-mode (external E1-E3 with trusted anchor; local S1-S7 via RUN_MODE=local), http_demo smoke — record PASS lists to specs/005-unified-openssl-backend/evidence/macos-arm64-runtime.md (SC-001/SC-003)
- [ ] T006 [US1] Confirm S3 memory-injection now flows through native blob capability without temp-file fallback: enable verbose or instrument check comparing CachedPemPath non-invocation (FR-005 行为迁移证据), annotate research.md D3 verification line
- [ ] T007 [US1] Android regression protection: rerun `make android_verify DEVICE=<serial>` unchanged (specs/004 assets), confirm zero impact from shared script generalization (T001 共用驱动的回归证明)

**Checkpoint**: US1 成立——行为矩阵跨平台逐项一致且既有套件零回归

---

## Phase 3: User Story 2 - 安全补丁职责的显式承接 (Priority: P1)

**Goal**: 版本升级只需触碰单一事实源文件并能在时限内完成全平台演练

**Independent Test**: 按 quickstart「版本升级演练」步骤执行一次（使用当前 pin 的空变更模拟），全程 ≤30 分钟且不改构建逻辑

### Implementation for User Story 2

- [ ] T008 [US2] Execute the upgrade drill from specs/005-unified-openssl-backend/quickstart.md verbatim as a dry-run (same-version no-op re-hash): touch cpp_network_deps.bzl entry → bazel clean → macos gtest + linux/android builds → record wall time and gaps found (SC-002), fix any discovered drift in quickstart text immediately
- [ ] T009 [US2] Add an upgrade-changelog anchor in docs/architecture/tls-config.md (当前版本/上一版本/变更日期 三行式小节) satisfying FR-002's "单一位置可回答"审计诉求

**Checkpoint**: 升级流程可复制、可计时、可审计——FR-002/SC-002 关闭

---

## Phase 4: User Story 3 - 移除对宿主系统传输层的隐性依赖 (Priority: P2)

**Goal**: 构建图零系统 libcurl 引用；Linux/macOS x86_64/aarch64 构建级验证入册

**Independent Test**: 在仅装基础编译工具的环境语义下（以 grep 断言无 -lcurl 残留近似替代）构建成功；`--config=linux_x86_64 / linux_aarch64 / macos_x86_64` 三个新配置构建退出码 0 并写入矩阵 verify_level=build-only

### Implementation for User Story 3

- [ ] T010 [US3] Static grep assertion tooling: add a `deps-audit` target in mk/help.mk-documented Makefile (or document one-liner) asserting zero occurrences of `-lcurl` across BUILD/bzl files (FR-003 的可执行断言)
- [ ] T011 [US3] Run cross-config builds: `bazel build --config=linux_x86_64 --config=... //src/public:cpp_network //src/tests:device_e2e` and `--config=macos_x86_64`, capture exit codes and artifact presence into specs/005-unified-openssl-backend/evidence/host-builds.md, then update TLSCapabilityMatrix rows with verify_level=build-only (FR-007; D4 Linux 运行通道说明保留)
- [ ] T012 [US3] Minimal-env behavioral probe equivalent: confirm curl configure stage inside genrule requires nothing beyond preinstalled base toolchain by running MODE=host genrule in a stripped-PATH sandbox; document required-tool list into evidence file (spec US3 场景 2)

**Checkpoint**: 可移植性达成有据：系统库引用清零 + 多配置构建产物实证

---

## Phase 5: User Story 4 - 第三方符号严格隐藏 (Priority: P2)

**Goal**: 共享形态产物导出表干净：第三方实现符号命中数为 0

**Independent Test**: 构建 `//src/public:cpp_network_shared` 后按 contracts/symbol-visibility.md 审计命令检查，命中计数为 0（SC-004）

### Implementation for User Story 4

- [ ] T013 [US4] Build //src/public:cpp_network_shared under --config=macos_arm64, create specs/005-unified-openssl-backend/evidence/symbol-audit-macos.md recording nm output summary, pass/fail against L1/L2/L3 rules (contracts/symbol-visibility.md)
- [ ] T014 [P] [US4] If audit reports leaks: iterate on compile flags/linkopts until clean, keeping changes within BUILD files and scripts touched by this feature (no new mechanism)
- [ ] T015 [US4] Record final audit conclusion into TLSCapabilityMatrix notes and link from spec US4 acceptance scenario (FR-006 关闭)

**Checkpoint**: 符号契约闭环——SC-004 达成

---

## Phase 6: Polish & Cross-Cutting Concerns

**Purpose**: 文档收口与 ADR/矩阵修订（FR-007/FR-008）

- [ ] T016 [P] ADR-003 third revision section in docs/architecture/adr/adr-003-tls-buildtime-select.md: full-platform source-built backend supersedes platform-following decision, cost statement including CVE follow-up ownership transfer, Windows exclusion boundary retained (FR-008)
- [ ] T017 [P] Refresh docs/architecture/tls-config.md platform table rows (BLOB quirk removal note for macOS; Linux trust-anchor same-as-others row) and tls-backend-selection.md matrix verify_level column (FR-007 落点)
- [ ] T018 Final validation pass: rerun `bazel test //...` + `make android_verify DEVICE=<serial>` once after all polish edits; append SC checklist conclusions into specs/005-unified-openssl-backend/evidence/final-report.md (data-model SymbolExportReport 附着点核对)

---

## Dependencies & Execution Order

### Phase Dependencies

- **Foundational (Phase 1)**: T001→T002→T003 严格串行（脚本→目标→验证）
- **US1 (Phase 2)**: ← Phase 1 完成后立即执行；是唯一不可逆动作所在阶段（回退=git revert）
- **US2 (Phase 3)**: ← Phase 2（翻转后的回归基线是演练前提）
- **US3 (Phase 4)**: ← Phase 2（select 收敛后 -lcurl 断言才有意义）；T011 的 linux 配置可与 T013 并行
- **US4 (Phase 5)**: ← Phase 2（需要 macos 配置下的 bundle 产物）
- **Polish (Phase 6)**: ← 其余阶段完成后收口

### Parallel Opportunities

- T013/T011 分属不同产物目录，可在 Phase 4/5 间穿插并行
- Phase 6 中 T016/T017 两份文档互不相干
- 无设备期间：T005 的 device_e2e external 模式退化为宿主执行仍可取证（fake-adb 通道沿用 004 经验）——但最终真机复验需设备在场

### Within Each User Story

先基建后验证；每个 Checkpoint 是独立停机点。

### Parallel Example: Phase 4 & 5

```bash
# 不同产物互不依赖，可并行推进：
Task: "linux_x86_64 build-only evidence (T011)"
Task: "shared symbol audit under macos config (T013)"
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Foundational
2. Complete Phase 2: US1 —— macOS 翻转 + 双端一致性取证
3. **STOP and VALIDATE**: 若任何场景结果漂移，立即 revert 重审 D1/D2 假设

### Incremental Delivery

1. Foundational → 能力就绪
2. + US1 → 行为一致性（核心价值落地）
3. + US2 → 升级机制可审计
4. + US3 → 可移植性证据链完整
5. + US4 + Polish → 符号契约与文档矩阵收官

### Notes

- git revert 单提交粒度 = 回退预案（research.md D5）
- 本特性不新增公共 API 表面；任务若发现需要改公共头文件即为范围信号，应回到 plan 层重新评估

---

## Notes

- 磁盘缓存冷热差异大：所有计取证的运行统一标注构建状态（warm/clean），避免误导性数字
- Linux 配置只承诺构建级成功，运行级验证留待对应宿主可用时执行（矩阵如实标注）
