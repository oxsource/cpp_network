# Tasks: C++ Cross-Platform Network Library (Architecture Design)

**Input**: Design documents from `/specs/001-cpp-network-library/`

**Prerequisites**: plan.md (required), spec.md (required for user stories), research.md, data-model.md, contracts/

**Scope**: 本阶段只产出**架构设计文档**，不涉及代码实现。所有任务产出物为设计文档（位于 `docs/architecture/`），供后续实现阶段作为输入。

**Tests**: 不包含测试任务（本阶段为架构设计，无代码测试）。

**Organization**: Tasks are grouped by user story to enable independent design and review of each story.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

## Path Conventions

- **Design docs**: `docs/architecture/` at repository root
- **ADR docs**: `docs/architecture/adr/`
- **Source layout reference**: plan.md `src/` structure (referenced, not created)

---

## Phase 1: Setup (Design Workspace)

**Purpose**: 初始化架构设计文档体系

- [x] T001 Create architecture design doc structure `docs/architecture/` and ADR subdir `docs/architecture/adr/`
- [x] T002 [P] Define design doc conventions and ADR template in `docs/architecture/README.md` (per plan.md structure)
- [x] T003 [P] Define Bazel workspace + platform definitions design (macos_arm64/macos_x86_64/linux_x86_64/linux_aarch64/android_arm64, `.bazelrc` `--config` aliases, mirroring graph_runtime) in `docs/architecture/bazel-platforms.md`

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: 所有用户故事共同的架构基础，必须先完成

**⚠️ CRITICAL**: No user story design can begin until this phase is complete

- [x] T004 Design 同步传输引擎 SyncEngine（共享 CURLM + curl_multi_poll 阻塞驱动、连接池复用、mutex 串行化）in `docs/architecture/sync-engine.md`（原 core-executor/curl-engine-bridge 已合并，异步抽象移除）
- [x] T005 [P] 废弃异步 Promise/Executor 抽象（同步重构：无 Then/Catch、无 WatchFd，见 research.md Decision 1）
- [x] T006 [P] Design Error taxonomy and `ErrorCode` enum (from contracts/public-api.md) in `docs/architecture/core-error.md`
- [x] T007 Design libcurl multi 同步驱动：`curl_multi_poll`/`curl_multi_perform` 阻塞循环、CURLMSG_DONE 完成检测、连接池复用，in `docs/architecture/sync-engine.md`（depends on T004）
- [x] T008 Design build-time TLS backend selection via Bazel `select()` (OpenSSL host / BoringSSL Android) and `src/tls/` layout in `docs/architecture/tls-backend-selection.md`

**Checkpoint**: 架构基础设计就绪，可开始各用户故事设计

---

## Phase 3: User Story 1 - Send HTTP Request and Receive Response (Priority: P1) 🎯 MVP

**Goal**: 设计 axios 风格**同步** HttpClient API 及 libcurl 传输层封装，支持 GET/POST 等请求与响应读取

**Independent Test**: 设计评审可独立验证 —— 检查 `docs/architecture/http-*.md` 是否覆盖 spec.md US1 的三个验收场景（200+body、404+自定义 header、POST JSON）

### Implementation for User Story 1

- [x] T009 [P] [US1] Design `HttpClient` public API (Get/Post/Put/Delete/Patch/Head/Options/Send 同步返回 `Result<HttpResponse>`, axios 风格) in `docs/architecture/http-client-api.md`
- [x] T010 [P] [US1] Design `HttpRequest` value type + Builder (method/url/headers/body/timeout override, 不可变性, `Result<HttpRequest>` Build) in `docs/architecture/http-request.md`
- [x] T011 [P] [US1] Design `HttpResponse` value type (status_code/status_text/headers/body_string/body_stream 同步流式) in `docs/architecture/http-response.md`
- [x] T012 [US1] Design 同步传输生命周期: 校验 → 进共享 CURLM → curl_multi_poll 阻塞驱动 → Result 返回 → handle 清理，in `docs/architecture/http-transfer-lifecycle.md` (depends on T004, T009)
- [x] T013 [US1] Design `NetworkConfig` → libcurl option 映射 (connect/read/write/total timeout、follow_redirects、max_redirects) in `docs/architecture/http-config-mapping.md`

**Checkpoint**: User Story 1 架构设计完整且可独立评审

---

## Phase 4: User Story 2 - Platform-Specific TLS Adapter (Priority: P1)

**Goal**: 设计基于 libcurl 构建时 SSL 后端选型（host=OpenSSL, Android=BoringSSL）的 TLS 方案与证书校验流程

**Independent Test**: 设计评审可独立验证 —— 检查 `docs/architecture/tls-*.md` 覆盖 spec.md US2 三个验收场景（macOS OpenSSL HTTPS、Android BoringSSL HTTPS、自签名证书 skip/默认拒绝）

### Implementation for User Story 2

- [x] T014 [P] [US2] Design `TlsConfig` type 与 `CURLOPT_SSL_*` 映射 (verify_mode/ca_certificates/client_certificate/sni_hostname) in `docs/architecture/tls-config.md`
- [x] T015 [P] [US2] Design 证书校验流程 (默认 kVerifyPeer、skip verification、自定义 CA) in `docs/architecture/tls-cert-validation.md`
- [x] T016 [US2] Design Android 平台 BoringSSL+libcurl 构建集成方案 (Bazel http_archive / NDK 预编译, API 24+) in `docs/architecture/android-boringssl-build.md`
- [x] T017 [US2] Design host 平台 OpenSSL+libcurl 构建集成方案 in `docs/architecture/host-openssl-build.md`

**Checkpoint**: User Story 2 架构设计完整，TLS 跨平台方案可独立评审

---

## Phase 5: User Story 3 - Configure Network Client Settings (Priority: P2)

**Goal**: 设计统一网络配置（NetworkConfig 实体 + 流式 Config builder），含重试、代理、连接池调优

**Independent Test**: 设计评审可独立验证 —— 检查配置设计覆盖 spec.md US3 三个验收场景（1s 连接超时、3 次重试、HTTP 代理路由）

### Implementation for User Story 3

- [x] T018 [P] [US3] Design `NetworkConfig` 实体与流式 `HttpClient::Config` builder in `docs/architecture/network-config.md`
- [x] T019 [P] [US3] Design `RetryPolicy` (max_retries/retry_delay/retry_condition, 默认不重试) in `docs/architecture/retry-policy.md`
- [x] T020 [P] [US3] Design HTTP 代理配置设计 in `docs/architecture/proxy-config.md`
- [x] T021 [US3] Design 连接池调优 (CURLMOPT_MAX_HOST_CONNECTIONS、keep-alive, 委托 libcurl) in `docs/architecture/connection-pool.md`

**Checkpoint**: User Story 3 架构设计完整

---

## Phase 6: User Story 4 - WebSocket Communication (Priority: P3)

**Goal**: 设计基于 libcurl 7.86+ 的 WebSocket 扩展方案及协议无关扩展机制（为后续实现留接口）

**Independent Test**: 设计评审可独立验证 —— 检查 WebSocket 设计覆盖 spec.md US4 两个验收场景（echo 消息收发、断线重连回调）

### Implementation for User Story 4

- [x] T022 [P] [US4] Design WebSocket API surface (libcurl 7.86+ websocket, 同步 Connect/Send/Receive/Close) in `docs/architecture/websocket-api.md`
- [x] T023 [P] [US4] Design 协议扩展机制 (SyncEngine 通用传输驱动复用, 新协议接入路径) in `docs/architecture/protocol-extension.md`
- [x] T024 [US4] Design WebSocket 消息流与断线重连处理 (同步) in `docs/architecture/websocket-message-flow.md`

**Checkpoint**: 所有用户故事架构设计完成

---

## Phase 7: Polish & Cross-Cutting Concerns

**Purpose**: 跨用户故事的设计收尾与一致性校验

- [ ] T025 [P] Consolidate 关键架构决策为 ADR 文档 in `docs/architecture/adr/`（异步模型、libcurl 选型、外部 Executor、TLS 后端选型等）
- [ ] T026 [P] 建立需求追踪矩阵：核对设计文档覆盖 spec.md FR-001..FR-021 与 SC-001..SC-008 in `docs/architecture/requirement-traceability.md`
- [ ] T027 [P] 校验 `quickstart.md` 示例与最终 API 设计文档一致
- [ ] T028 审查平台无关性保证 (FR-016)：所有设计文档确认无平台 ifdef 泄漏到公共 API

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phase 3+)**: All depend on Foundational phase completion
  - 用户故事设计可并行（按人员分工）或按优先级串行（P1 → P2 → P3）
- **Polish (Final Phase)**: Depends on all desired user stories being complete

### User Story Dependencies

- **User Story 1 (P1)**: Can start after Foundational (Phase 2) - No dependencies on other stories
- **User Story 2 (P1)**: Can start after Foundational (Phase 2) - 依赖 T008 (TLS backend selection)
- **User Story 3 (P2)**: Can start after Foundational (Phase 2) - 与 US1 的 T013 config mapping 关联但独立评审
- **User Story 4 (P3)**: Can start after Foundational (Phase 2) - 依赖 T007 (curl engine bridge)

### Within Each User Story

- 设计文档间有引用依赖（如 transfer lifecycle 依赖 engine bridge）时，被依赖文档先完成
- Story complete before moving to next priority

### Parallel Opportunities

- 所有 Setup 任务标记 [P] 可并行
- Foundational 中 [P] 任务（T005/T006）与串行任务 (T004→T007, T008) 可分派
- Foundational 完成后所有用户故事可并行设计
- 各 Story 内的 [P] 文档任务可并行
- 不同用户故事可由不同评审者并行推进

---

## Parallel Example: User Story 1

```bash
# Launch all design docs for User Story 1 together:
Task: "Design HttpClient public API in docs/architecture/http-client-api.md"
Task: "Design HttpRequest value type in docs/architecture/http-request.md"
Task: "Design HttpResponse value type in docs/architecture/http-response.md"

# Then (depends on T007):
Task: "Design transfer lifecycle in docs/architecture/http-transfer-lifecycle.md"
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (CRITICAL - blocks all stories)
3. Complete Phase 3: User Story 1
4. **STOP and VALIDATE**: 评审 US1 设计是否覆盖 spec 验收场景
5. 若满足即可交付 MVP 设计，US2 可并行或随后

### Incremental Delivery

1. Complete Setup + Foundational → 基础设计就绪
2. Add User Story 1 → 评审 → 交付 MVP 设计
3. Add User Story 2 → 评审 → 交付 TLS 方案
4. Add User Story 3 → 评审 → 交付配置方案
5. Add User Story 4 → 评审 → 交付 WebSocket 扩展方案
6. 每个 story 独立评审，互不阻塞

### Parallel Team Strategy

With multiple designers:

1. Team completes Setup + Foundational together
2. Once Foundational is done:
   - Designer A: User Story 1
   - Designer B: User Story 2
   - Designer C: User Story 3
3. Stories review independently

---

## Notes

- [P] tasks = different files, no dependencies
- [Story] label maps task to specific user story for traceability
- 本阶段产出为设计文档，交付物路径统一在 `docs/architecture/`
- 每个 checkpoint 均可独立评审 story 设计完整性
- Commit after each task or logical group
- 避免：模糊任务、同文件冲突、破坏独立性的跨 story 依赖
- 后续 `/speckit.implement` 将以本 tasks.md 及 `docs/architecture/` 文档为输入
- **2026-08-26 同步重构**：架构由异步（Promise/Executor/WatchFd）改为**同步阻塞 API**（用户决策）。`core-executor.md`/`core-promise.md`/`curl-engine-bridge.md` 已移除并合并为 `sync-engine.md`；contracts/data-model/spec 已同步更新。T004/T005/T007 描述已改写反映新架构。
