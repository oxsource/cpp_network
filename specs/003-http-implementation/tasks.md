# Tasks: 设计实现并验证 HTTP

**Input**: Design documents from `/specs/003-http-implementation/`

**Prerequisites**: plan.md (required), spec.md (required for user stories), research.md, data-model.md, contracts/

**Tests**: 本提案明确要求验证（FR-009/010/014），含集成测试任务（TDD 风格：先写测试，再实现）。

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

## Path Conventions

- 公共头：`src/public/include/netlib/`；实现：`src/http/`；测试：`src/tests/`。
- 参考 `contracts/public-api.md` 的 API 签名与 `plan.md` 目录结构。

---

## Phase 1: Setup（工程初始化）

**Purpose**: 构建接入系统 libcurl/OpenSSL，替换 src/http 占位

- [ ] T001 [P] Update `src/http/BUILD.bazel`：新增 `client`/`engine` 等 cc_library 目标，链接系统 `-lcurl`（linkopts），保持零 warning
- [ ] T002 [P] Update `src/public/include/netlib/netlib.h`：umbrella 头 include client/request/response/options/tls/error/result
- [ ] T003 [P] Update `src/public/BUILD.bazel`：`netlib` 目标 hdrs 含新增公共头，deps 指向 `src/http` 实际目标
- [ ] T004 Create `src/http/error.h` + `src/http/error.cc`（ErrorCode 枚举 + Error 类型 + CURLcode→ErrorCode 映射函数，参考 contracts public-api.md 与 001 core-error.md）

---

## Phase 2: Foundational（阻塞性基础，US 前置）

**Purpose**: 核心值类型与 Result——所有用户故事依赖

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

- [ ] T005 Create `src/http/result.h`（`Result<T>`：ok/value/error/TakeValue/Ok/Err，参考 contracts）
- [ ] T006 [P] Create `src/http/method.h`（`Method` 枚举：kGet/kPost/kPut/kDelete/kPatch/kHead/kOptions）
- [ ] T007 Create `src/http/request.h` + `src/http/request.cc`（`Request` + Builder：method/url/headers/body/JsonBody/timeout，校验 URL 绝对/CRLF 注入/method-body 约束 → kInvalidArgument）
- [ ] T008 Create `src/http/response.h` + `src/http/response.cc`（`Response`：status/status_text/headers/body/effective_url/ok，含流式 Stream 句柄声明）
- [ ] T009 Create `src/http/options.h` + `src/http/options.cc`（`Options`：超时/重定向/网卡(interface/local_address/local_port)/代理/连接池/Tls，Validate）
- [ ] T010 Create `src/http/tls.h` + `src/http/tls.cc`（`Tls` + VerifyMode：CA(PEM/文件)/mTLS/SNI/skip）
- [ ] T011 [P] Update `src/public/include/netlib/netlib_export.h` 确认 NETLIB_API 宏（既有，无需改，仅确认）

**Checkpoint**: 值类型与 Result 就绪，用户故事可开始

---

## Phase 3: User Story 1 - HTTP 请求/响应 (Priority: P1) 🎯 MVP

**Goal**: Client 同步 API + 同步引擎 + HTTP 集成测试（200/404/POST-JSON）

**Independent Test**: 本地 HTTP 测试服务器上 `Client::Get/Post` 返回正确状态码/headers/body；`bazel test` 集成测试通过

### Tests for User Story 1

- [ ] T012 [P] [US1] Create `src/tests/test_server.py`（本地 HTTP 测试服务器 fixture：支持 /（200 Hello World）、/404（404 + X-Custom header）、/echo（POST 回显 body + Content-Type））
- [ ] T013 [US1] Create `src/tests/http_integration_test.cc`（用例：200+body、404+header、POST JSON 回显，参考 spec US1 验收场景 1-3）

### Implementation for User Story 1

- [ ] T014 [US1] Create `src/http/engine.h` + `src/http/engine.cc`（同步引擎：共享 CURLM + curl_multi_poll 阻塞驱动 + 连接池复用 + mutex 串行化，参考 research Decision 3 / 001 sync-engine.md）
- [ ] T015 [US1] Create `src/http/detail/curl_mapping.cc`（`Request`/`Options` → CURLOPT 映射：method/url/headers/body/timeout，参考 research Decision 4）
- [ ] T016 [US1] Create `src/http/client.h` + `src/http/client.cc`（`Client`：Create(Options)/Get/Post/Put/Delete/Patch/Head/Options/Send/Close，Send 校验 Request → engine 执行 → 构造 Response，参考 contracts public-api.md）
- [ ] T017 [US1] Create `src/http/response_stream.cc`（大 body >8MB 流式 Stream 实现：Read/Skip，同步块读）
- [ ] T018 [US1] Update `src/public/include/netlib/` 头文件引用（client/request/response/options/tls/error/result/method 实际暴露）

**Checkpoint**: US1 MVP 达成——HTTP 可发可收，集成测试通过

---

## Phase 4: User Story 2 - HTTPS + 证书配置 (Priority: P2)

**Goal**: OpenSSL TLS + 证书配置（CA/mTLS/SNI/skip）+ HTTPS 验证

**Independent Test**: 远程 HTTPS 端点 200；自签证书默认校验失败、注入 CA/skip 后成功；mTLS 握手成功

### Tests for User Story 2

- [ ] T019 [P] [US2] Create `src/tests/test_tls_server.py`（HTTPS 测试服务器：自签证书，支持 mTLS 模式，用 python ssl 模块）
- [ ] T020 [P] [US2] Create `src/tests/certs/`（测试证书：自签 CA + 服务端证书 + 客户端证书，用 openssl CLI 生成并随测试打包）
- [ ] T021 [US2] Create `src/tests/https_test.cc`（用例：远程 HTTPS 200、自签默认失败(kCertificateVerificationFailed)、注入 CA 成功、skip 成功、mTLS 成功）

### Implementation for User Story 2

- [ ] T022 [US2] Update `src/http/detail/curl_mapping.cc`：Tls → CURLOPT_SSL_* 映射（CAINFO[_BLOB]/SSLCERT/SSLKEY/SNI/VERIFYPEER/VERIFYHOST，参考 research Decision 6）
- [ ] T023 [US2] Update `src/http/client.cc`：Options 的 Tls 应用到每次传输（Client 构建时保存 Tls，Send 时映射）

**Checkpoint**: US2 HTTPS 与证书配置验证通过

---

## Phase 5: User Story 3 - 网络配置生效（超时/重定向/指定网卡）(Priority: P3)

**Goal**: Options 配置正确映射：超时/重定向/指定网卡

**Independent Test**: 1s 连接超时 → kConnectionTimeout；302 跟随 → effective_url；指定网卡源地址正确

### Tests for User Story 3

- [ ] T024 [P] [US3] Create `src/tests/config_test.cc`（用例：连接超时(kConnectionTimeout)、重定向跟随(effective_url)、读超时(kReadTimeout)，参考 spec US3 验收场景 1-2）

### Implementation for User Story 3

- [ ] T025 [US3] Update `src/http/detail/curl_mapping.cc`：Options → CURLOPT 映射补充（CONNECTTIMEOUT_MS/TIMEOUT_MS/LOW_SPEED/FOLLOWLOCATION/MAXREDIRS/INTERFACE/LOCALPORT，参考 research Decision 5）
- [ ] T026 [US3] Update `src/http/options.cc`：SetInterface/SetLocalAddress/SetLocalPort 校验（非法 → kInvalidArgument）

**Checkpoint**: US3 配置生效验证通过

---

## Phase 6: Polish & Cross-Cutting Concerns

**Purpose**: 全量验证、符号可见性、文档

- [ ] T027 [P] 验证 `bazel build //...` 零 error/warning（含新增目标）
- [ ] T028 [P] 验证 `bazel test //...` 全部通过（smoke + http_integration + https + config）
- [ ] T029 验证公共 API 不暴露 libcurl 类型（检查 src/public/include/netlib 头无 curl/CURL/curl_slist）
- [ ] T030 验证共享库符号仅 NETLIB_API（`bazel build //src/public:netlib_shared` + nm 检查）
- [ ] T031 [P] 更新 `quickstart.md` 示例与最终 API 一致（GET/POST/证书/网卡）

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: 无依赖，可立即开始
- **Foundational (Phase 2)**: 依赖 Setup — BLOCKS 所有用户故事
- **User Stories (Phase 3+)**: 依赖 Foundational；US1→US2→US3 可部分并行
- **Polish (Final Phase)**: 依赖所有用户故事完成

### User Story Dependencies

- **US1 (P1)**: 依赖 Foundational（Request/Response/Options/Result）；不依赖其他故事
- **US2 (P2)**: 依赖 Foundational（Tls/Options）+ US1（Client 引擎）——在 US1 Client 基础上加 TLS 映射
- **US3 (P3)**: 依赖 Foundational（Options）+ US1（Client 引擎）——在 US1 基础上补配置映射

### Within Each User Story

- 测试（T012/T013 等）先写，实现后运行验证
- 值类型 → 引擎 → Client → 测试
- 测试服务器 fixture（T012）先于集成测试（T013）

### Parallel Opportunities

- Setup T001/T002/T003 标记 [P] 可并行
- Foundational T006/T011 [P] 与 T005/T007/T008/T009/T010 可并行
- US2 测试 T019/T020 [P] 可与 US1 实现并行（不同文件）
- US3 测试 T024 [P] 可与 US1/US2 并行

---

## Parallel Example: User Story 1

```bash
# Launch tests + test server first (TDD):
Task: "Create test_server.py HTTP fixture"
Task: "Create http_integration_test.cc"

# Launch engine + client together (different files):
Task: "Create engine.h/engine.cc"
Task: "Create curl_mapping.cc"
Task: "Create client.h/client.cc"
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. 完成 Phase 1 + Phase 2（Setup + Foundational）
2. 完成 Phase 3（US1）：引擎 + Client + 集成测试
3. **STOP and VALIDATE**: `bazel test //...`（smoke + http_integration）
4. 成功即交付 MVP（HTTP 可发可收）

### Incremental Delivery

1. Setup + Foundational → 值类型就绪
2. US1 → HTTP 核心（MVP）
3. US2 → HTTPS + 证书配置
4. US3 → 网络配置（超时/重定向/网卡）
5. Polish → 全量验证 + 符号检查

### Parallel Team Strategy

1. 团队共同完成 Setup + Foundational
2. Foundational 后：
   - 开发者 A: US1（引擎/Client/HTTP 测试）
   - 开发者 B: US2 测试环境（TLS 服务器 + 证书）
3. US2/US3 在 US1 引擎就绪后接入

---

## Notes

- [P] tasks = different files, no dependencies
- [Story] label maps task to specific user story for traceability
- 实现遵循 `contracts/public-api.md` API 签名与 `plan.md` 目录
- 系统 libcurl/OpenSSL 链接（research Decision 1）；third_party 源码构建留后续
- 每个 checkpoint 可独立验证 story 完成
- Commit after each task or logical group
- 避免：同文件冲突、破坏独立性的跨 story 依赖
