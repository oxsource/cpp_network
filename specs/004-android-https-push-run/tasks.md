# Tasks: Android HTTPS 支持与一键设备部署运行

**Input**: Design documents from `/specs/004-android-https-push-run/`

**Prerequisites**: plan.md (required), spec.md (required for user stories), research.md, data-model.md, contracts/

**Tests**: 设备端 e2e 检查程序（S1–S7）是本特性的功能性交付物（FR-005），不属可选测试；宿主侧 gtest 全量回归为既有验收基线。

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

## Path Conventions

仓库为单一 Bazel workspace：源码在 `src/`、第三方在 `third_party/`、构建入口在 `Makefile`+`mk/`，设备工具依赖宿主侧 `adb`。

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: 让 NDK 工具链与构建依赖在 workspace 中可用（所有后续阶段的前提）

- [x] T001 Register rules_foreign_cc dependency and `android_ndk_repository(name="androidndk", path=$ANDROID_NDK_HOME)` in WORKSPACE; confirm `bazel query @androidndk//...` resolves when ANDROID_NDK_HOME is set (per plan.md D3)
- [x] T002 [P] Extend tools/platform_setup.sh with NDK r26+ and adb presence checks, printing actionable hints (export ANDROID_NDK_HOME / PATH) per specs/004-android-https-push-run/research.md D6-D7

**Checkpoint**: 工具链可见性就绪——`@androidndk` 可解析、platform_setup 输出含 NDK 状态行

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: 打通「Android 架构下 OpenSSL+curl 源码交叉编译并注入 cpp_network」的完整依赖图；所有用户故事都阻塞于此

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

- [x] T003 Implement third_party/openssl/BUILD.bazel android branch: rules_foreign_cc `configure_make` building pinned OpenSSL 3.0.13 (third_party/openssl/openssl.bzl) into static `ssl`/`crypto` targets under config_setting android_arm64, keeping the host stub intact via select() (research.md D1/D2)
- [x] T004 Implement third_party/libcurl/BUILD.bazel android branch: autotools build of pinned curl 8.7.1 with `--with-openssl=<openssl install>` + `--enable-static --disable-shared` + protocol trim to HTTP/HTTPS (`--disable-*` extras per research.md D2); expose `curl` cc_library target (depends on T003)
- [x] T005 Switch src/http/BUILD.bazel engine/client linkopts to select(): host keeps `-lcurl`; android_arm64 links the source-built curl target (T004); then verify `bazel build --config=macos_arm64 //src/public:cpp_network` still passes and `bazel build --config=android_arm64 //src/public:cpp_network` produces arm64-v8a outputs (FR-010 zero-regression gate)

**Checkpoint**: Foundation ready——cpp_network 在 android 架构可编译链接；host 全量行为零变化。US1 的手动验证与 US2 的命令化目标均可从此起步

---

## Phase 3: User Story 1 - Android 设备上发起并验证 HTTPS 请求 (Priority: P1) 🎯 MVP

**Goal**: 使用库的公共 API 在 Android 设备上完成四类 HTTPS 场景 + HTTP 基线验证，错误语义与主机平台一致

**Independent Test**: 手动执行（不经 make）：`adb push` 产物与证书 → `adb reverse tcp:18443 tcp:18443` 等 → 宿主启动测试服务 → `adb shell` 运行 device_e2e → 期望 `PASS 7/7`

### Implementation for User Story 1

> **NOTE**: 先落资产路径覆写点，再写场景编排；场景清单与退出码协议见 data-model.md Entity 4 与 contracts/device-test-contract.md

- [x] T006 [P] [US1] Add test-asset root resolution honoring `NETLIB_TEST_DATA_DIR` env override (default repo-relative `src/tests/certs/`) as a shared helper header in src/tests/, and switch cert path construction in src/tests/https_test.cc to use it (contracts/device-test-contract.md 覆写点，宿主行为不变)
- [x] T007 [US1] Implement src/tests/device_e2e.cc: self-contained scenario orchestration S1–S7 using public API only (default-reject / SetCaFile / SetCaPem / mTLS without client cert / mTLS with client cert / kSkipVerification / HTTP 404 baseline) against `127.0.0.1:<ports>` bases overridable via env (data-model.md Entity 4, exit-code = first-failing-scenario-id+1, continue-on-failure, final `PASS <n>/<total>` line)
- [x] T008 [US1] Register device_e2e cc_binary in src/tests/BUILD.bazel linking //src/http:client + public_headers so it builds for both host (smoke) and android_arm64
- [ ] T009 (BLOCKED: no device attached — run `adb devices` and connect/authorize one) [US1] Manually validate all four HTTPS scenarios on a real device/emulator following contracts/device-test-contract.md topology (test servers stay on host; adb reverse channels; record evidence for FR-010): default reject error matches host ErrorCode, CA file/in-memory/mTLS/skip succeed (SC-001)

**Checkpoint**: 不依赖任何 make 目标，即可在设备上复现与主机一致的 HTTPS 行为——US1 独立成立（spec US1 验收场景 1–4 全部通过）

---

## Phase 4: User Story 2 - 开发者一条命令完成 Android 构建产物 (Priority: P1)

**Goal**: 拉取仓库后按文档执行单一命令即产出 cpp_network 与设备端程序，首次与增量路径均可用

**Independent Test**: 干净环境（无 bazel 缓存）执行 `make build-android` → 退出码 0 且产物齐备；再次执行明显更快（SC-002 增量部分由 US3 闭环覆盖）

### Implementation for User Story 2

- [x] T010 [US2] Create mk/android.mk defining `build-android` target that delegates to `bazel build --config=android_arm64 //src/public:cpp_network //src/tests:device_e2e //src/examples/http_demo:http_demo`, surfacing bazel failures verbatim; register description text through mk/rules.mk conventions (contracts/make-targets.md Targets 表)
- [x] T011 [US2] Add `ANDROID_NDK_HOME` prerequisite guard inside build-android: fail fast with hint to run tools/platform_setup.sh when unset or missing (US2 acceptance scenario 1 preflight)
- [x] T012 [US2] Validate fresh-environment reproducibility of build-android (clear third-party build caches, rerun, record wall time) and compare with incremental run for docs evidence (SC-002; research.md D7 matrix evidence ②)

**Checkpoint**: 新环境到产物的命令序列可复制、无隐藏手工步骤；host 默认 config 未受影响

---

## Phase 5: User Story 3 - make push&run 一键部署到设备执行 (Priority: P2)

**Goal**: `push`→`run` 一条龙闭环：推送产物、建立转发、远程执行、实时回显、退出码透传

**Independent Test**: 连接一台授权设备后 `make push && make run` 得到 `[device-exit: 0]` 且输出实时流式回传；拔线重跑得到立即失败与排查指引（SC-005）

### Implementation for User Story 3

- [x] T013 [US3] Implement `push` target in mk/android.mk: resolve latest built artifacts, `adb [-s $DEVICE] push` binaries and src/tests/certs/ into `$DEVICE_DIR` (/data/local/tmp/cpp_network/), printing per-file remote-path bytes summary lines (contracts/make-targets.md Output 契约); stale-artifact guard requires re-run of build-android (data-model.md BuildArtifact 校验规则)
- [x] T014 [US3] Implement device-selection logic shared by push/run/clean-device: DEVICE overrides ANDROID_SERIAL overrides auto-pick; enumerate via `adb devices`; 0 devices → connection hint; unauthorized/offline entries reported distinctly; ≥2 candidates → list and abort (contracts/make-targets.md Device 选择规则 1–4, US3 场景 3–4)
- [x] T015 [US3] Implement `run` target in mk/android.mk: launch host test servers (:18080/:18443:44 wait-ready loop), establish `adb reverse tcp:N tcp:N` for each PORTS entry, execute device_e2e remotely, stream stdout/stderr live, parse trailing `EXIT:<code>` line → propagate as make exit status and print `[device-exit: <code>]` summary (contracts/make-targets.md Exit Codes)
- [x] T016 [US3] Implement `clean-device` target removing $DEVICE_DIR contents idempotently (missing dir counts as success) (contracts/make-targets.md Targets 表)
- [x] T017 [US3] Wire mk/android.mk into Makefile includes; verify `make help` lists build-android/push/run/clean-device with descriptions (mk/help.mk mechanism, contracts/make-targets.md 输出契约末条)
- [x] T018 [US3] End-to-end closed-loop validation of push+run incl. failure paths: no-device error timing ≤5s, duplicate-port conflict reporting with offending port name, data-cable-unplug mid-push leaves no bad references (rerun recovers) — record results against spec Edge Cases list and SC-005

**Checkpoint**: 「改代码 → 构建 → push → run → 出结果」≤2 分钟达成（SC-003）；多设备/无设备/端口冲突交互符合契约

---

## Phase 6: User Story 4 - 无根权限与环境自适应的可移植性保障 (Priority: P3)

**Goal**: 零 root、跨厂商设备可复现同一闭环；确认产物全部落在 shell 可写私有目录

**Independent Test**: 在一台未定制的不同品牌设备上重复 US3 流程一次通过（spec US4 场景 1–2）

### Implementation for User Story 4

- [ ] T019 [US4] Audit artifact placement: assert all writes confined to $DEVICE_DIR (/data/local/tmp/, shell-writable, no system partition access) across push/run/clean-device scripts in mk/android.mk (US4 场景 1 precondition)
- [ ] T020 [US4] Run the full push+run closure twice on two different-vendor devices and diff PASS summaries; capture any device-specific divergence into follow-up notes (spec US4 场景 2, SC-001 cross-device repeat)

**Checkpoint**: 可移植性证据成立；US4 独立可测且不影响前序故事

---

## Phase 7: Polish & Cross-Cutting Concerns

**Purpose**: 文档矩阵更新与全量回归收口（对应 FR-010/FR-011）

- [ ] T021 [P] Update docs/architecture/tls-config.md 平台差异表 Android row to实测结论（信任锚=显式注入,CA 目录不可直读的约束说明）and tls-backend-selection.md 验证矩阵（Android arm64 已实测通过）linking this feature dir (research.md D7, FR-011)
- [ ] T022 [P] Record decision trail addendum in docs/architecture/adr/adr-003-tls-buildtime-select.md or linked note: Android 分支落地选用 OpenSSL 3.0.13 及 Conscrypt/NDK 白名单不可用论证（research.md D1 补充论证）
- [ ] T023 Run full host regression suite `bazel test //...` on macOS arm64 confirming zero behavior change beyond新增目标 (FR-010), and `make verify` passes end-to-end
- [ ] T024 Walk through specs/004-android-https-push-run/quickstart.md verbatim in a clean terminal session fixing any drift found (quickstart must remain copy-paste runnable, SC-002 closing evidence)

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: T001/T002 无相互依赖，立即可做
- **Foundational (Phase 2)**: T003←(T001)；T004←(T003)；T005←(T004)。阻塞全部用户故事
- **US1 (Phase 3)**: ← Phase 2。仅依赖 bazel 直调，不依赖 make 目标
- **US2 (Phase 4)**: ← Phase 2；与 US1 并行可行（不同文件域）
- **US3 (Phase 5)**: ← US2（build-android 目标）+ US1（device_e2e 产物与其退出码协议）
- **US4 (Phase 6)**: ← US3（复用 push/run 闭环）
- **Polish (Phase 7)**: ← 各故事完成后收口

### Within Each User Story

先覆写点/基础设施，再场景实现，最后注册与真机验证；同一故事内 [P] 任务可并行。

### Parallel Opportunities

- T001 与 T002（Setup 内）
- US1 与 US2 各自独立成故事，Foundational 完成后可由两人并行
- Phase 7 中 T021/T022 两份文档互不相干可并行
- device_e2e 场景实现完成后，T009 的四个验收场景可在一次设备会话内串行取证

---

## Parallel Example: User Story 1

```bash
# 先并行完成资产覆写点（宿主侧小改动）：
Task: "Add NETLIB_TEST_DATA_DIR resolution in src/tests/ helper + https_test.cc"
# 然后（依赖 T006）单线程推进场景编排，避免同文件冲突：
Task: "Implement src/tests/device_e2e.cc S1-S7"
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational（Android 构建链打通）
3. Complete Phase 3: US1 —— 手动 adb 流程验证四类 HTTPS 场景
4. **STOP and VALIDATE**: spec US1 验收场景 1–4 全绿后，核心能力已交付

### Incremental Delivery

1. Setup + Foundational → 构建链就绪
2. + US1 → 设备端能力可证（MVP）
3. + US2 → 命令化构建（复现性达标）
4. + US3 → push/run 日常回归闭环（效率达标）
5. + US4 + Polish → 可移植性与矩阵证据收口

### Notes

- [P] tasks = 不同文件、无未完成依赖
- 每个 Checkpoint 都是可停机验证点；建议逐任务或逻辑组提交
- 设备相关验证（T009/T018/T020）需要真实连接的设备；无设备时可用官方模拟器先行，但最终验收以真机为准（spec Assumptions）

---

## Notes

- 本特性无纯单测要求新增之外：S1–S7 属功能交付而非可选测试
- 所有第三方版本锁定不得漂移（third_party/*.bzl），遇上游补丁需求走升级评审而非原地修改
