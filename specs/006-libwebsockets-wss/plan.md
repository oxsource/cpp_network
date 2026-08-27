# Implementation Plan: 集成 libwebsockets——WebSocket（ws+wss 双通道）

**Branch**: `006-libwebsockets-wss` | **Date**: 2026-08-27 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `/specs/006-libwebsockets-wss/spec.md`

## Summary

以**源码构建的 libwebsockets v4.5.8（静态链接，指向既有 TLS bundle 的 OpenSSL）**为传输内核，交付全平台一致的同步阻塞 WebSocket 客户端：加密（wss）与明文（ws）双通道、完整消息语义收发、限时双向关闭与对端关闭详情透传。新增第三方组件纳入 `cpp_network_deps.bzl` 单一事实源治理并沿用 twin-package genrule 构建模式；原占位模块落地于 `src/websocket`，公开类型 `cpp_network::http::WebSocket`。

## Technical Context

**Language/Version**: C++17；Bazel 6.5.0；构建驱动延续 005 的 genrule 模式（本特性第三方侧改用 CMake，见 research D2 前提）

**Primary Dependencies**: libwebsockets v4.5.8（新增 pin，sha256 实测）；OpenSSL 3.0.13 + curl 8.7.1（pin 不变）；libwebsockets 仅依赖 bundle 中已暴露的 `//third_party/openssl/{host,android}:openssl` 切片——无第二 TLS 栈（spec FR-003）

**Storage**: N/A

**Testing**: 新增 gtest 套件 `websocket_test`（宿主运行级）+ device_e2e 扩展 W 场景（真机运行级）；本地回声 fixture 复用既有自签证书体系

**Target Platform**: macOS arm64（运行级验收基线）、macOS x86_64 / Linux x86_64 / Linux aarch64（构建级）、Android arm64（真机运行级）；Windows 维持排除

**Project Type**: C++ 库能力扩展（HTTP(S) 之后第二个协议面）

**Performance Goals**: 环回"连接—文本回声—二进制回声—关闭"p95 ≤100ms（SC-001）；升级演练总耗时 ≤30min（SC-004，含新组件重建）

**Constraints**: 版本 pin 不得漂移；既有 gtest 断言零删改（FR-011）；单一事实源锁版本流程不变；逐请求不引入回调式公共 API（FR-002 同步纪律）

**Scale/Scope**: 新增 src/websocket 实现 + 公共头 + 构建驱动 + 测试资产；预计触及 ~15 文件；功能面为增量新增，不动既有 HTTP 路径

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

Constitution 为模板、无已定义原则，无违规可评估。Gate: PASS。（Phase 1 设计复检：沿用库形态+同步 API+测试先行扩展点，无新增违规。）

## Project Structure

### Documentation (this feature)

```text
specs/006-libwebsockets-wss/
├── plan.md              # This file
├── research.md          # Phase 0 output (D1–D8 决策)
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output (public API + device scenario)
└── tasks.md             # Phase 2 output (/speckit.tasks)
```

### Source Code (workspace root: `cpp_network/`)

> 以下文件路径均相对仓库根的 **`cpp_network/`** Bazel 工作区（仓库根另含 `specs/`）。

```text
cpp_network_deps.bzl                      # +lws 条目（字母序）
third_party/libwebsockets/BUILD.bazel     # libwebsockets.BUILD 注入声明
third_party/libwebsockets/libwebsockets.BUILD
third_party/scripts/build_libwebsockets.sh          # MODE=android|host CMake 驱动（对照 build_openssl.sh）
third_party/libwebsockets/{host,android}/BUILD.bazel  # twin-package genrule + :websockets 目标
src/websocket/
├── BUILD.bazel                           # 重写占位（cc_library "websocket"，alwayslink）
└── websocket.cc                          # Impl：lws_context/wsi 封装、状态机、缓冲
src/public/include/http/
├── websocket.h                           # 公开类型 WebSocket/WsMessage/WsCloseCode
├── export.h                              # 无改动（沿用 NETLIB_API）
└── http_umbrella.h                       # +#include websocket.h
mk/android.mk / Makefile                  # make 目标无需新增（device_e2e 扩展即可）
src/tests/
├── test_ws_server.py                     # 新增 ws/wss 回声 fixture（RFC6455 最小实现）
├── websocket_test.cc                     # 新增 gtest 套件（P1–P3 场景映射）
└── device_e2e.cc                         # 追加 W1–W… 场景（向后兼容协议）
```

**Structure Decision**: 单项目库结构内做模块化扩展；第三方构建完全复制 005 已验证的 twin-package/genrule 形态，仅把 configure/make 换成 cmake；公开头随既有 `src/public/include/http/` 目录与 `http` 命名空间惯例（001 定型的 `cpp_network::http::Xxx`）。

## Complexity Tracking

> 无违规条目。
