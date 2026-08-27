# Research: Android HTTPS 支持与一键设备部署运行

**Branch**: `004-android-https-push-run` | **Date**: 2026-08-27

本文档解决 spec 与 plan 中的全部开放决策。背景依据：ADR-003 第二次修订（TLS 后端跟随平台，Android 集成启动时决策）、`docs/architecture/tls-backend-selection.md`、`android-boringssl-build.md`（001 历史稿）。

## D1: Android TLS 加密后端选型

**Decision**: OpenSSL 3.0.13 LTS（复用 `third_party/openssl/openssl.bzl` 已锁定版本），源码交叉编译、静态链接进 libcurl。

**Rationale**:
- 版本已在 001/002 工程结构阶段 pin 定（URL + SHA256），无版本漂移风险；LTS 支持窗口长
- Linux 主机路径同为 OpenSSL 语义栈，CURLOPT 的证书/mTLS/SNI 行为跨平台最一致（FR-001 的行为一致性约束最容易达成）
- libcurl 对 `--with-openssl` 路径是兼容性验证最充分的组合
- License (Apache-2.0) 对静态链接友好

**Alternatives considered**:
- **BoringSSL**：源自 Android 生态、体量小，但为快速演进分支无 LTS；001 阶段曾因其 Bazel 8 属性与 Bazel 6.5 不兼容被记录废弃（绕开其 bazel 构建改走 CMake 可规避，但为非官方主路径）；CA 校验细节与 OpenSSL 存在已知差异面。Rejected。
- **mbedTLS**：交叉编译最容易，但与 curl 组合的实战验证少、高级特性（SNI 覆盖、部分 TLS1.3 行为）支持面窄于需求。Rejected。
- **系统 libssl/libcrypto（NDK 直链）**：NDK 自 r18 起不再暴露任何 ssl/crypto 库。不可行。

**补充论证（2026-08-27 决策澄清）**: "能否使用 Android 平台自己的 TLS 体系"的可行性边界：

1. 系统/Java 层的 TLS 是 Conscrypt（Java Provider，底层 BoringSSL），接口仅面向 JVM 调用方；libcurl 无 Conscrypt 后端，C++ 进程不可直接调用。
2. NDK 自 r18 起彻底移除 libssl/libcrypto，且应用进程受 linker namespace 限制只能链接 `/system/etc/public.libraries.txt` 白名单内的系统库——TLS 库不在其中，直链系统 BoringSSL 同样不可行。
3. 因此任何 C++ 方案都必须源码自建一个 TLS 后端；"Android 自己的 TLS"的实际等价物是 BoringSSL（Android 系统内部实现），但其工程量与 OpenSSL 相当（同样源码静态编译），主要差异在版本锁定方式（commit hash vs 正式 tag）与 API 稳定性承诺（无 LTS）。经决策评审维持 OpenSSL 3.0.13：LTS 正式版本锁定、与 Linux 主机同语义栈、libcurl 兼容性验证最充分。

## D2: Android 上 libcurl 的获取方式

**Decision**: libcurl 8.7.1 源码 autotools 构建（复用已 pin 版本），`--with-openssl=<openssl install>`、`--enable-static --disable-shared`、协议裁剪到 HTTP/HTTPS（`--disable-*` 其余协议与 brotli/zstd/idn2/ssh 等可选依赖），产物静态库经 `cc_library` 依赖注入 netlib。

**Rationale**: Android 无发行版可用的系统 curl；源码构建是唯一可控路径。裁剪协议显著缩短构建时间并缩小符号面；仅保留 spec US1 所需能力。

**Alternatives considered**:
- **预编译产物入库**：体积大、许可证与供应链审计差、无法随 NDK 升级重建。Rejected。
- **curl 的 CMake 构建路径**：功能等价；选 autotools 是因 openssl.bzl 同走 configure/make 家族，工具链变量传递方式统一。Rejected（等价备选）。

**构建机制**: `rules_foreign_cc`（Bazel 6.5 兼容线）的 `configure_make` / `autotools` 规则承载两个第三方项目；NDK 工具链由 `android_ndk_repository` 提供的 clang wrapper（`CC/CXX/AR/RANLIB` 及 sysroot flags）透传。

## D3: NDK 工具链接入 Bazel

**Decision**: `android_ndk_repository(name = "androidndk", path = "$ANDROID_NDK_HOME")` + 既有 `--config=android_arm64 --platforms=//platforms:android_arm64`；要求环境变量 `ANDROID_NDK_HOME` 指向 r26+；`tools/platform_setup.sh` 增加存在性检测与缺失提示。宿主默认 config（macos_arm64）不变，Android 侧所有第三方目标仅在 android 分支激活。

**Rationale**: 平台/constraint 层早已就绪（platforms/BUILD），缺口只在仓库规则与工具链解析；Bazel 6.5 内建 ndk repository 虽标记 deprecated 但在该版本稳定可用，避免引入 rules_android 全家桶的迁移成本。文档明示未来升级 Bazel 时替换点。

**Alternatives considered**:
- **rules_android_ndk（社区版）**：更前向但引入新依赖链，本特性交付风险高。Deferred。
- **手写 cc_toolchain_suite**：完全可控但维护成本高且易错。Rejected。

## D4: Android 上的信任锚策略

**Decision**: v1 不内置 CA bundle、不读 `/system/etc/security/cacerts`：依赖既有公共 API 的信任锚注入能力（`Tls::Builder::SetCaFile/SetCaPem`），并在 TLS 文档中写明 Android 差异（FR-003/FR-011）。设备端 e2e 使用推送至设备的测试 CA 文件路径完成"文件形态"用例、"内存 PEM 形态"用例无需外部文件。

**Rationale**:
- Android 系统 CA 目录文件带元数据头、无 c_hash 命名，`CURLOPT_CAINFO`（单 bundle）/`CAPATH`（hash 名）两者都不能直接消费——自动装载需要合并转换逻辑，超出 v1 且属于应用层职责
- ADR-003 已确立"App 注入信任锚"为该场景的设计意图；保持 API 一致即可
- `SetCaFile` 在 Android 分支走 libcurl 文件路径（无 macOS 的 blob 运行时回退问题），内存 PEM 走 `*_BLOB` 或 CachedPemPath 回退，两态在 src/http/detail/curl_mapping.cc 已实现无需改动

**Alternatives considered**: 应用启动时遍历合并系统目录生成 bundle（可行但涉及 SELinux 权限差异面），Deferred 至后续版本文档指引。

## D5: 设备端验证程序与资产定位

**Decision**: 新增独立 `src/tests/device_e2e.cc`（自包含客户端场景编排、聚合退出码），不把既有 gtest 集成套件原样搬上设备：
- 既有集成测试在设备上不可直接运行——它们 fork 宿主机 python3 测试服务器，Android shell 无 python3
- e2e 将四类 HTTPS 场景 + HTTP 基线对准 `127.0.0.1:<port>`，端口与宿主服务之间经 `adb reverse` 打通；测试服务器与证书全部驻留宿主（FR-009）
- 测试资产根目录引入环境变量覆盖：`NETLIB_TEST_DATA_DIR`（默认沿用现仓库相对路径 `src/tests/certs/...`）；设备端运行时设为 `/data/local/tmp/cpp_network/certs`
- 宿主侧 Google Test 套件继续作为全量回归；Android 架构的 gtest 编译通过列为构建验收项之一

**Rationale**: 测试服务器留驻宿主使设备端二进制最小化（免 python/脚本分发），同时真实穿透 adb reverse 通道模拟应用联网路径；单二进制 + 明确退出码契合 make run 的自动化契约。

**Alternatives considered**: 把测试服务器整体跑在设备端（需 termux 类环境或移植 server 为 C++）——复杂度不成比例。Rejected。

## D6: make push / run 目标设计

**Decision**: 新增 `mk/android.mk`（沿用仓库 mk 模块化约定），目标契约详见 [contracts/make-targets.md](contracts/make-targets.md)：
- `make build-android`：构建 Android 架构下的 netlib 与设备端可执行程序（device_e2e、http_demo）
- `make push [DEVICE=serial]`：构建 → `adb [-s] push` 产物与 certs 到 `/data/local/tmp/cpp_network/`
- `make run [DEVICE=serial]`：前置 `adb reverse tcp:18080/tcp:18443`（可通过 PORT 变量扩展）→ `adb shell` 执行 → 解析 `EXIT:<code>` 透传退出码、输出实时回传
- 设备选择：DEVICE 显式优先；未指定时枚举 `adb devices`，0 台报错提示授权排查、≥2 台列出候选终止（US3 场景 3/4）

**Rationale**: adb reverse 让设备用 `127.0.0.1` 访问宿主服务，免除 IP/网段发现逻辑；退出码透传（`adb shell 'cmd; echo EXIT:$?'` 并 grep）保证 CI 化使用。mk/*.mk 模式与仓库既有 help/rules 注册机制一致。

**Alternatives considered**: gradle 封装工程——引入整套 Android 应用构建栈，与"开发回归闭环"定位不符（spec Assumptions 已排除打包上架）。Rejected。

## D7: 五平台矩阵更新方式

**Decision**: FR-010 通过三处证据落地：① `tools/platform_setup.sh` 输出 NDK 状态；② `specs/004-.../quickstart.md` 提供完整命令序列；③ 相关架构文档平台差异表将 Android 行更新为实测结论（附验收命令）。不改动 host 构建分支的行为与依赖图。

**Rationale**: 保持矩阵更新有据可查而非口头声明；host 侧零回归由 select() 结构性保证。
