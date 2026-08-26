# Research: 工程结构与依赖库组织

**Branch**: `002-engineering-structure` | **Date**: 2026-08-26

## Decision 1: 工作区布局 — 单主工作区 + 独立消费者 demo

- **Decision**: 仓库根为主 Bazel 工作区（`workspace(name = "netlib")`）；`examples/consumer_demo/` 为独立工作区，经 `local_repository` 依赖主工作区，并被主工作区 `.bazelignore` 排除。
- **Rationale**: 完全镜像 graph_runtime 模式。主工作区自洽构建；消费者 demo 独立验证"外部工程如何依赖本库"（FR-012）。
- **Alternatives considered**:
  - 单一工作区含消费者示例：rejected — 无法演示真实的 local_repository 消费路径。
  - MONOREPO 多包（无独立 demo 工作区）：rejected — 丢失外部消费者验证价值。

## Decision 2: 依赖引导宏 netlib_deps.bzl（幂等）

- **Decision**: 采用 graph_runtime 的 `graph_runtime_deps.bzl` 模式——单一 `netlib_setup()` 宏，经 `native.existing_rule()` 守卫每个依赖（重复调用不报错），全部经 `http_archive` 拉取且带 sha256 校验。
- **Rationale**: 幂等守卫是 graph_runtime 验证过的模式，避免 WORKSPACE 重复定义报错；sha256 保证可复现（SC-006）。
- **依赖清单（版本锁定）**:
  | 依赖 | Bazel 名 | 版本 | 用途 |
  |------|----------|------|------|
  | bazel_skylib | bazel_skylib | 1.6.1 | 构建辅助 |
  | libcurl | curl | ≥7.86（源码） | 协议引擎 |
  | OpenSSL | openssl | 3.x LTS（源码） | host TLS |
  | BoringSSL | boringssl | 固定 commit | Android TLS |
  | Google Test | googletest | 1.14.0 | 测试 |

## Decision 3: third_party 封装 — 每依赖独立目录 + libcurl 双后端变体

- **Decision**: `third_party/<dep>/BUILD.bazel` + `<dep>.bzl`（版本锁定）独立封装。libcurl 提供两个目标：`libcurl_openssl`（host，`USE_OPENSSL`）与 `libcurl_boringssl`（android）。
- **Rationale**: graph_runtime 的 nlohmann_json 封装先例 + 001 提案 tls-backend-selection 设计。BoringSSL 兼容 OpenSSL API 命名空间，libcurl 以 `USE_OPENSSL` 宏编译即可（BoringSSL 提供 `SSL_*`/`X509_*` 符号）。
- **Alternatives considered**: 单 libcurl 目标 + 运行时切换：rejected — 后端在构建时固定（001 ADR-003）。自研 TlsAdapter：rejected — 001 已否决。

## Decision 4: 平台抽象 — platforms.bzl 宏 + 五平台

- **Decision**: 复用 graph_runtime 的 `config_setting_and_platform(name, constraint_values)` 宏；`platforms/BUILD` 定义 macos_arm64/macos_x86_64/linux_x86_64/linux_aarch64/android_arm64。`.bazelrc` 提供 `--config=<platform>` 别名；`netlib_select()` 供 TLS 后端 select。
- **Rationale**: 001 提案 bazel-platforms.md 已定案此设计；graph_runtime 验证过实现。
- **注意**: Android 平台用 `@platforms//os:android` + `@platforms//cpu:aarch64`；host 平台与 graph_runtime 完全一致。

## Decision 5: 平台设置脚本 tools/platform_setup.sh

- **Decision**: 检测 `uname -s`（Darwin/Linux）+ `uname -m`（arm64/x86_64/aarch64），映射到平台名，写入 git-ignored 的 `.user.bazelrc`（`build --config=<platform>`）。未知架构 → 报错退出。
- **Rationale**: 镜像 graph_runtime `platform_setup.sh`。`.user.bazelrc` 经 `.bazelrc` 的 `try-import` 加载，低优先级覆盖（FR-005/edge case）。
- **Alternatives considered**: CI 生成 `.configure.bazelrc`：deferred — v1 仅本机脚本。

## Decision 6: 便利构建入口 Makefile + mk/

- **Decision**: 复刻 graph_runtime 的 AOSP 风格模块注册（`mk/rules.mk` 的 `register_module`/`register_target`/`register_alias`），`mk/aliases.mk` 提供 `build`/`test`/`verify`/`clean` 目标，内部转调 Bazel。
- **Rationale**: graph_runtime 验证过的开发者体验（FR-010）。`make verify` = 依赖解析 + build + test。

## Decision 7: 公共库目标 //:netlib（静态 + 共享）

- **Decision**: `src/public/BUILD.bazel` 提供 `:netlib`（cc_library，静态）与 `:netlib_shared`（`cc_binary(linkshared=True, linkstatic=True)`）。共享库以 `-DNETLIB_SHARED_LIBRARY` + `-fvisibility=hidden` + `NETLIB_API` 标注符号导出。
- **Rationale**: 001 提案 bazel-platforms.md 的 `netlib_export.h` 设计 + graph_runtime `runtime_shared` 先例。根 `BUILD.bazel` 提供 `alias //:netlib`。

## Decision 8: 源码目录占位结构

- **Decision**: 按 001 提案目录结构（src/http、src/websocket、src/tls、src/public/include/netlib、src/examples、src/tests）各建 BUILD 与最小占位头/源，保证 `bazel build //...` 零错误。实际协议实现留后续阶段。
- **Rationale**: FR-006 要求结构就位；占位目标让工程骨架可构建可测，避免空目录导致 BUILD 失败。

## Open Questions (deferred, 非阻塞)

- libcurl 源码在 Bazel 内构建的具体方式（预生成 curl_config.h vs configure 脚本）：实现阶段确定，倾向预生成 `curl_config.h`（host/android 各一份，镜像 001 提案 android-boringssl-build.md 方案 A2）。
- OpenSSL 3.x 具体小版本：实现阶段锁定（3.0.x 或 3.3.x）。
- 是否需要 nlohmann_json（001 提案可选）：v1 工程骨架暂不引入。
