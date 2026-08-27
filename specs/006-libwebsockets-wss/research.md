# Research: specs/006-libwebsockets-wss

日期：2026-08-27；执行机：Mac mini M4（macOS 26 / CLT）；网络经 `127.0.0.1:7890` 代理。

## D1 版本选型与 pin

**Decision**: libwebsockets **v4.5.8**（stable 4.5 分支最新），pin 于 `cpp_network_deps.bzl`：

```text
name: lws
urls: https://github.com/warmcat/libwebsockets/archive/refs/tags/v4.5.8.tar.gz
sha256: b6ade658f4af3a823d0dc806ae5ef0623f0f4f5e2aeb895a0f77c4783840c30e   # 本机实测
strip_prefix: libwebsockets-4.5.8
```

**Rationale**: 与 OpenSSL LTS 同型的保守策略——不追 v5.0.0 新主版本（2026 发布，生态未沉淀）；4.5.x 为长期维护线。GitHub tags API 实测确认 4.5 分支活跃且 v4.5.8 为其头部。

**Alternatives considered**: curl 的 `curl_ws_*`（001 v2 草案方案）——被用户决策否决（本特性指令点名 libwebsockets；且绑定 curl ≥7.86 与我们锁定的 8.7.1 冲突面更小但仍弃用）；uWebSockets/beast 等非 C 库或强依赖框架者不符合库定位。

## D2 构建形态：CMake 驱动 + twin-package genrule

**Decision**: 复制 005 已验证形态——`third_party/libwebsockets/{host,android}/BUILD.bazel` 双包 + 共享驱动 `third_party/scripts/build_libwebsockets.sh`（MODE=android|host），内部调 **CMake**。

**构建前提（风险与缓解）**: lws 仅支持 CMake 构建，而本机现无 cmake → 作为平台前提安装（brew install cmake / apt install cmake），并写入 `tools/platform_setup.sh` 检查项；Bazel action 以 `--action_env=PATH` 显式携带工具路径（对照 macos_x86_64 的 TLS_HOST_ARCH 注入模式，避免沙箱 PATH 缺失）。

**特性裁剪（对应 spec FR/排除项）**:
```text
-DLWS_WITHOUT_SERVER=ON          # 客户端-only（Assumptions）
-DLWS_WITHOUT_EXTENSIONS=ON      # permessage-deflate 排除（FR-012）
-DLWS_WITH_SHARED=OFF            # 静态链接，同 bundle 约定
-DLWS_WITH_SSL=ON -DOPENSSL_ROOT_DIR=<bundle-install>  # 唯一 TLS 栈
-DLWS_WITHOUT_TESTAPPS=ON -DLWS_WITHOUT_TEST_SERVER=ON
-DLWS_ROLE_WS=1 -DLWS_ROLE_H1=1  # 仅 ws 承载所需角色
-DLWS_WITH_ZLIB=OFF -DLWS_WITH_LIBUV/EV/EVENT=OFF  # 内置 poll 事件环即可
-DLWS_STATIC_PIC=ON              # 对象层 fPIC，随共享库策略
```

**Android 交叉要点**: cmake 工具链经由导出的 CC/CXX/AR/RANLIB（NDK llvm 工具链，与 build_openssl.sh android 分支同一套导出）生效，无需独立 toolchain 文件；`-DANDROID` 由前缀 clang 三元组触发探测。

## D3 TLS 集成（FR-003 单栈纪律）

**Decision**: wss 经由 lws 原生 SSL 路径，SSL_CTX 底座为 bundle 的 OpenSSL；信任锚与客户端材料走 lws 的**内存接口**：
`info.client_ssl_ca_mem / client_ssl_ca_mem_len`、`client_ssl_cert_mem / client_ssl_key_mem`——天然满足"内存 PEM 直达、无临时文件"语义（specs/005 收口成果直接复用）。

**对 spec 澄清 Q2/B 的落实**: ws:// 明文连接时 lws 以非 SSL 套接字建立；映射层把 TLS 配置**整体短路忽略**（不传入 info 字段），实现"静默忽略、文档明示"。wss 默认拒绝自签 = 不设 ca_mem 时 lws 默认校验行为（与本机系统锚不含自签一致）→ 与 HTTP 平台基线逐项对齐由证据文件背书。

## D4 同步包装架构（FR-002）

**Decision**: 每 WebSocket 连接一个 `lws_context`（`lws_create_context`+`lws_client_connect_via_info`），调用线程以 `lws_service(context, timeout_ms)` 阻塞泵驱动；Send/Receive/Close 全部在泵上完成：

| 操作 | 机制 |
|------|------|
| Connect | 泵至 CONNECTION_ESTABLISHED 回调（受连接超时约束），失败路径 EXPIRED/CLIENT_CONN_ERROR |
| Send | 入写队列（`lws_write` 在 WRITABLE 回调中发出），阻塞至全部写出 |
| Receive | 泵至 RX 回调累积入消息缓冲（按 `lws_is_final_fragment` 组帧），满一整条即返回 |
| Close | 发 close 帧 → 泵限时等 peer 关闭；对端先关则 CLOSE 回调写入关闭码/原因（澄清 Q3/B 的详情来源） |

状态机沿用 message-flow 草案四态；单线程使用边界即"一个 context 一个泵线程"，天然免跨线程锁（Assumptions）。

**为什么不做 context 共享复用 HTTP 引擎的 CURLM**: 两套事件环模型不可调和（lws 要求自主 service 循环）；spec Assumptions 已明示不强求共享轮询引擎，进程内共存即可。

## D5 明文/加密双通道路由

**Decision**: `Connect` 解析 scheme：仅接受 `ws://` 与 `wss://`，其余快速失败（FR-001）。lws 连接参数 `ssl_connection` 标志位按通道设置（LCCSCF_USE_SSL）；`ws://` 携带 TLS 配置静默短路（D3）。默认端口推断 443/80。

## D6 依赖治理与审计面扩展

**Decision**: `cpp_network_deps.bzl` 新增字母序条目 `lws`（http_archive + build_file 注入）；升级演练 quickstart 流程不变（touch 驱动脚本 + clean + verify）；`deps_audit` 断言扩为同时零 `-lcurl` 直链与零 `-lwebsockets` 绕道（防止下游绕过 bundle 手拼系统库）。

## D7 测试资产与场景契约

**Decision**:
- 新 fixture `test_ws_server.py`：stdlib 手写 RFC6455 最小回声服务（握手 SHA1/Base64 accept-key + 帧编解码，支持文本/二进制/分段/主动 close/探活帧注入开关），wss 形态套用既有 `src/tests/certs/{server,ca}_*.pem` 自签体系（mTLS 服务端形态复用 test_tls_server.py 的 CERT_REQUIRED 模式）
- gtest `websocket_test.cc` 场景 ↔ spec US 映射：W1 受信连接成功 / W2 明文连接成功 / W3 自签拒默认 / W4 内存 PEM 注入通过 / W5 非法 scheme 快速失败 / W6–W9 收发与分段重组 / W10 关闭握手幂等 / W11 对端关闭详情读取 / W12 探活透明（P3 子集可 device 剪裁）
- `device_e2e.cc` 追加 `W*` 场景（外部模式走公网 wss echo，local 模式走 on-device fixture），输出协议向后兼容（新增 PASS 行不破坏既有解析）

## D8 公共 API 定形（对应 contracts/）

**Decision**: `src/public/include/http/websocket.h`，命名空间 `cpp_network::http`，类型 `WebSocket`（实例方法 Connect 改为静态工厂 → 对齐现有 `HttpClient` 构造风格）、`WsMessage{data,is_text}`、`WsCloseCode`（标准码枚举）；错误经既有 `Result<T>`/`Error` 体系；NETLIB_API 导出标注；伞头追加 include。关闭详情以结构化字段挂于 Error 附加载荷（error.h 扩展最小化：附加 `close_code/reason` 可选字段仅在 websocket 错误类填充）。
