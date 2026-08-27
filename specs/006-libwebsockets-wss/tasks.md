# Tasks: 集成 libwebsockets——WebSocket（ws+wss 双通道）

**Input**: Design documents from `/specs/006-libwebsockets-wss/`

**Prerequisites**: plan.md ✅ | spec.md ✅ | research.md ✅ | data-model.md ✅ | contracts/ ✅ | quickstart.md ✅

**Tests**: 包含测试任务——spec 的 SC-002/003/005 与验收场景即测试契约（各平台一致性与回归闸）。

## Format: `[ID] [P?] [Story] Description`

- **[P]**: 可并行（不同文件、不依赖未完成任务）
- **[Story]**: 所属用户故事（US1–US4，映射 spec.md 四故事）
- 路径均为仓库相对路径

## Path Conventions

单项目库形态：`src/`（库实现）、`third_party/`（三方构建）、`tools/` 与 `mk/`（工作流）、`docs/architecture/`（设计文档）。

---

## Phase 1: Setup（共享基础设施）

**Purpose**: 三方组件纳入单一事实源治理并具备构建驱动

- [x] T001 在 cpp_network_deps.bzl 注册 `lws` 条目（字母序置于 curl 之后）：`https://github.com/warmcat/libwebsockets/archive/refs/tags/v4.5.8.tar.gz`，sha256=`b6ade658f4af3a823d0dc806ae5ef0623f0f4f5e2aeb895a0f77c4783840c30e`（research D1 实测值），strip_prefix=`libwebsockets-4.5.8`，`build_file` 用 `@cpp_network//third_party/libwebsockets:lws_external.BUILD`；创建 third_party/libwebsockets/BUILD.bazel 包标记与 lws_external.BUILD（filegroup 导出全部源与 CMakeLists，对照 openssl_external.BUILD 形态）
- [x] T002 [P] 扩展 tools/platform_setup.sh：新增 cmake 存在性与版本下限检查（缺失时给出安装指引的 NOTICE 行，风格同 NDK 解析提示）
- [x] T003 编写 third_party/scripts/build-lws.sh：MODE=android|host 的 CMake 构建驱动，参数 `<openssl-install-dir> <install-dir>`；特性裁剪按 research D2 清单（WITHOUT_SERVER/EXTENSIONS/TESTAPPS、STATIC=ON、SSL=ON 且 OPENSSL_ROOT_DIR 指向传入目录、STATIC_PIC=ON）；android 分支复用 build-tls.sh 的 NDK 导出集（CC/CXX/AR/RANLIB/PATH）

## Phase 2: Foundational（阻塞性前置）

**Purpose**: 构建链打通 + 公共类型骨架，全部用户故事依赖于此

**⚠ 先决**: Phase 1 完成（T003 被 T004 直接消费）

- [x] T004 创建 third_party/lws/host/BUILD.bazel：genrule 以 build-lws.sh host 构建（install 目录产出 lib/libwebsockets.a 与 include/lws 公共头清单作为 outs），cc_library `lws`（静态包 + hdrs + strip_include_prefix="include"）；macos_arm64 下 `bazel build //third_party/lws/host:lws` 通过且归档为 Mach-O arm64（证据存 specs/006-libwebsockets-wss/evidence/build-matrix.md）
- [x] T005 创建 third_party/lws/android/BUILD.bazel 双胞胎包（genrule 走 MODE=android，约束同 tls/android 的 target_compatible_with）；`bazel cquery --config=android_arm64 //third_party/lws/android:lws` 分析通过（真机构建级验证在 US4/T020 完成闭路）
- [x] T006 扩展 src/public/include/http/error.h：Error 增加可选附加载荷字段 `close_code(uint16_t, 0=未知)` 与 `close_reason(string)`（仅 websocket 层填充，data-model.md CloseInfo），补充 `kConnectionClosed` 错误码（若枚举中不存在）；HTTP 现有错误取值集不变（FR-011 前提确认）
- [x] T007 创建 src/public/include/http/websocket.h：`WsMessage{data,is_text}`、`WsCloseCode`(1000/1001/1002/1003/1005/1006)、`WebSocket` 类声明（静态 Connect(url, NetworkConfig)/IsOpen/Send/Receive/Close，私有 Impl 共享指针），NETLIB_API 标注并对齐 contracts/websocket-api.md 签名；http_umbrella.h 追加 include；src/websocket/BUILD.bazel 从空占位重写为实目标（deps: //src/tls:tls、@public_headers、//third_party/lws:{host,android}:lws 经 select 路由同 src/http/_TLS_DEPS 模式）并入 engine/client 的 alwayslink 链

## Phase 3: User Story 1 —— 建立 WebSocket 连接（ws+wss 双通道）(Priority: P1)

**Story Goal**: 任一受支持平台上对安全/明文回声服务完成双通道连接闭环，TLS 基线五场景全绿。

**Independent Test**: `python3 src/tests/test_ws_server.py` 两实例（明文+自签tls）启动后运行 `bazel test //src/tests:websocket_test`，W1–W5 全 PASS。

- [x] T008 [P] [US1] 编写 src/tests/test_ws_server.py：stdlib 手写 RFC6455 最小回声服务（SHA1/Base64 accept-key 握手、文本/二进制帧编解码、分段发送开关、`--port/--tls --certs-dir/--require-client-cert/--inject-ping/--peer-close CODE REASON` 模式旗标）；wss 复用 src/tests/certs 自签证书体系
- [x] T009 [US1] 在 src/websocket/websocket.cc 实现 `WebSocket::Connect`：scheme 白名单校验（非 ws/wss 零网络活动返回 kInvalidArgument）；wss 时把已 Validate 的 Tls 映射为 lws context info 的 client_ssl_ca_mem/client_ssl_cert_mem/client_ssl_key_mem 内存注入（research D3），ws:// 时整体短路忽略；单连接独立 lws_context + 泵至 CONNECTION_ESTABLISHED（连接超时生效）；失败路径区分证书校验失败/连接失败错误码
- [x] T010 [US1] 新增 src/tests/websocket_test.cc 并在 src/tests/BUILD.bazel 注册 `websocket_test` 目标（data deps: fixture 启动脚本，TestMain 复用 test_util.h 的端口/进程管理惯例）：W1 受信 wss 连接成功、W2 明文 ws 连接成功、W3 自签默认拒（kCertificateVerificationFailed）、W4 内存 PEM 注入通过、W5 非法 scheme 快速失败；`bazel test //src/tests:websocket_test` 5/5

## Phase 4: User Story 2 —— 消息收发（文本与二进制） (Priority: P1)

**Story Goal**: 连接建立后文本/二进制完整消息原子收发，长消息分段对调用方透明，探活帧不出现在消息流。

**Independent Test**: 沿用 US1 fixture，`websocket_test` 中 W6–W9 全 PASS（MVP 扩展为连接+收发可演示闭环）。

- [x] T011 [US2] 实现 `WebSocket::Send`（src/websocket/websocket.cc）：状态门槛（OPEN 否则 kInvalidState 快速失败）、经 lws 写队列在 WRITABLE 回调分片写出直至整条完成（大消息循环泵），零长度载荷合法直发
- [x] T012 [US2] 实现 `WebSocket::Receive`（src/websocket/websocket.cc）：RX 回调按 `lws_is_final_fragment` 组帧入消息缓冲，满一条即返回 WsMessage；探活帧由 lws 默认机制应答、不入消息流（research D4/D7 对照断言点）
- [x] T013 [US2] 扩充 websocket_test.cc：W6 文本回声往返、W7 二进制回声往返、W8 fixture 分段模式 8MB 载荷一次 Receive 取整条（FR-005）、W9 `--inject-ping` 下探活透明性（真实消息不失序）；目标保持全绿

## Phase 5: User Story 3 —— 关闭与状态收敛 (Priority: P2)

**Story Goal**: 双向限时关闭握手 + 幂等 + 对端关闭详情透传，异常后调用快速失败不挂起。

**Independent Test**: fixture `--peer-close 1000 bye` 模式下 W10/W11 全 PASS；主动/被动两向覆盖。

- [x] T014 [US3] 实现 `WebSocket::Close(WsCloseCode, reason)` 与状态机收敛（src/websocket/websocket.cc）：CLOSING 态限时等待对端关闭（关闭等待上限 FR-009）；重复 Close 幂等返回不异常；对端 CLOSE 回调将 code/reason 写入 Error 附加载荷（T006 字段），后续 Receive 返回 kConnectionClosed 详情；IsOpen 仅 OPEN 为真（CONNECTING/CLOSING/CLOSED 均 false）
- [x] T015 [US3] 扩充 websocket_test.cc：W10 主动正常关闭握手完成且随后 Send/Receive 快速 kInvalidState/kConnectionClosed、第二次 Close 幂等；W11 fixture 主动 close(1000,"bye") 后 Receive 错误详情码=1000 且 reason=bye；未连接先 Send/Receive/Close 的快速失败断言

## Phase 6: User Story 4 —— 平台一致性与依赖治理 (Priority: P3)

**Story Goal**: 依赖升级演练覆盖新组件、审计闸扩展、真机端到端场景与文档定稿。

**Independent Test**: `make android_verify DEVICE=<serial>` 含 W 段全绿退出 0；演练计时写证据。

- [x] T016 [US4] mk/android.mk 的 deps_audit 断言扩展：除零 `-lcurl` 外同时零 `-lwebsockets` 直链绕道；`make deps_audit` 通过
- [x] T017 [US4] 扩展 src/tests/device_e2e.cc（contracts/device-scenarios.md）：追加 W 场景段（external 模式公网 wss echo 回声、local 模式 on-device fixture 双通道+mTLS+对端关闭详情），输出 `[W*] PASS : …` 行与分组小计，既有 S/E 段与退出码协议不变（工具 android_device.sh 若需挂载新 fixture 同步更新）
- [x] T018 [P] [US4] 重写 docs/architecture/websocket-api.md 与 docs/architecture/websocket-message-flow.md：删除 curl_ws_* 草案内容，替换为 libwebsockets v4.5.8 实现语义（同步四操作、状态机、内存 PEM 直达、ws 明文静默忽略 TLS 说明），命名收敛 `cpp_network::http::WebSocket`
- [x] T019 [P] [US4] 新增 docs/architecture/adr/adr-004-websockets-transport.md：记录"libwebsockets vs curl_ws_*"选型决策、单一 TLS 栈纪律（bundle :openssl 复用）与 CMake 工具链前提；docs/architecture/README.md 索引同步
- [x] T020 [US4] 终验三件套并写 specs/006-libwebsockets-wss/evidence/：① `bazel test //...` 全绿（套件数 6→7，既有断言零删改）；② `make android_verify DEVICE=<serial>` W 段全绿（真机 be11）；③ 升级演练 `touch build-lws.sh && bazel clean && make verify && make android_build` 计时与 SC-001 环回 p95 采样记录

---

## Dependencies

```text
T001 ─┐
T003 ─┼─► T004 ──► T007 ──► T009(T010↔互促) ──► T011/T012(T013) ──► T014(T015) ──► T016–T020
T002 ─┘   T005        T006 ────────────────┘（T014 消费其字段）
```

- T008 仅依赖测试基础设施，可与 T003–T007 并行提前完成
- T010 起每故事阶段的 gtest 任务逐级叠加于前态之上（同一测试文件串行编辑）
- T016–T019 相互独立；T020 必须最后执行

## Parallel Execution Examples

- **Foundational 窗口**：T002 ∥ T005 ∥ T006（三个不同文件域）
- **US1 内部**：T008(fixture) 与 T009(Connect 实现) 可并行会合于 T010
- **收尾窗口**：T018 ∥ T019（纯文档）∥ T016

## Implementation Strategy

- **MVP**：Phase 1+2 + US1（T001–T010）——交付"双通道可连接+安全基线"，独立演示闭环
- **增量交付**：US2 补齐消息面（完整可用），US3 补生命周期收敛（生产可用），US4 为治理收尾
- **回归闸**：每个阶段结束跑一次 `bazel test //...`，保证 FR-011（既有 6 套件断言零删改）
