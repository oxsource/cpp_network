# Research: Android HTTPS 支持与一键设备部署运行

**Branch**: `004-android-https-push-run` | **Date**: 2026-08-27

本文档解决 spec 与 plan 中的全部开放决策。背景依据：ADR-003 第二次修订（TLS 后端跟随平台，Android 集成启动时决策）、`docs/architecture/tls-backend-selection.md`、`android-boringssl-build.md`（001 历史稿）。

## D1: Android TLS 加密后端选型

**Decision**: OpenSSL 3.0.13 LTS（复用 `cpp_network_deps.bzl` 中锁定的版本），源码交叉编译、静态链接进 libcurl。

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

**Decision**: libcurl 8.7.1 源码 autotools 构建（复用已 pin 版本），`--with-openssl=<openssl install>`、`--enable-static --disable-shared`、协议裁剪到 HTTP/HTTPS（`--disable-*` 其余协议与 brotli/zstd/idn2/ssh 等可选依赖），产物静态库经 `cc_library` 依赖注入 cpp_network。

**Rationale**: Android 无发行版可用的系统 curl；源码构建是唯一可控路径。裁剪协议显著缩短构建时间并缩小符号面；仅保留 spec US1 所需能力。

**Alternatives considered**:
- **预编译产物入库**：体积大、许可证与供应链审计差、无法随 NDK 升级重建。Rejected。
- **curl 的 CMake 构建路径**：功能等价；选 autotools 是因 curl 与 openssl 同走 configure/make 家族，工具链变量传递方式统一。Rejected（等价备选）。

**构建机制**: `rules_foreign_cc`（Bazel 6.5 兼容线）的 `configure_make` / `autotools` 规则承载两个第三方项目；NDK 工具链由 `android_ndk_repository` 提供的 clang wrapper（`CC/CXX/AR/RANLIB` 及 sysroot flags）透传。

**T003/T004 实施修订（2026-08-27）**: 落地时改为 **宿主侧构建脚本 + genrule** 方案（`third_party/scripts/build_openssl.sh`，单一 genrule 于 `//third_party/openssl/host` 固定命名产出 libcrypto/libssl/libcurl.a 与 12 个 `include/curl/*.h` 公共头，经 `cc_library(name="curl")` 暴露）。动机：
1. rules_foreign_cc 的 include 产物为 TreeArtifact（哈希化路径），无法向下游原生 cc 规则提供稳定的 `-I` 引用；链式两段构建还要解决动态 env 注入，调试面大。
2. 脚本方案在沙箱内直接消费 `--action_env=ANDROID_NDK_HOME`，编译命令全程可见、失败易定位；实测 openssl(19s)+curl(22s) 干净构建约 40s。
其余约束不变：版本 pin 不动、协议裁剪清单一致、host 分支零触碰。rules_foreign_cc 注册保留作后备。另两个附带修正已实测验证：① `@curl` 改用官方发布包（GitHub 归档缺预生成 configure）；② `.bazelrc` 移除 `--enable_platform_specific_config`——它会在命令行解析后重注宿主平台配置，静默覆盖显式交叉 config。⚠️ 取证注意：toolchain-resolution 模式下 Bazel 输出目录沿用宿主命名（darwin_arm64-fastbuild），产物实际为 Android ELF，必须用 `file` 判定而非目录名。

## D3: NDK 工具链接入 Bazel

**Decision（T001 实施后定稿）**: 采纳 video_codec workspace 已验证的方案——`rules_android_ndk` v0.1.2 提供支持现代 NDK 布局的 `android_ndk_repository(name="androidndk")`；其懒取仓特性保证无 NDK 宿主不触碰该仓库。NDK cc_toolchain 仅经 `.bazelrc` 在 android config 下注册：

```text
build:android_arm64 --extra_toolchains=@androidndk//:all
build:android_arm64 --incompatible_enable_cc_toolchain_resolution=true
```

同时采用其同仓库验证过的 `rules_foreign_cc_dependencies(register_built_tools=False, register_built_pkgconfig_toolchain=False)`——依赖宿主预装 make/pkg-config，跳过 cmake/ninja 源码构建。

**T001 验证证据**: ① 未设置 ANDROID_NDK_HOME 时 host 构建绿色（懒取仓生效）；② ANDROID_NDK_HOME=NDK 28.2.13676358 时 `bazel query @androidndk//:all` 解析出全部工具链；③ `bazel build --config=android_arm64 //src/public:cpp_network` 下 NDK clang 真实进入源码交叉编译（当前仅缺 Phase 2 将提供的 curl 头文件/静态库，属预期）。

**演进记录**: 初版尝试 Bazel 内建 `android_ndk_repository` 被否决——无 NDK/残留安装时急切失败破坏 FR-010、且对 r18+ 移除的旧式 platforms/ 目录误校验；过渡期的自研探测规则方案随后被本参考方案整体取代（探测逻辑的能力由后续 mk/build-android 目标的 shell fail-fast 承担）。

**Alternatives considered**:
- **rules_android_ndk（社区版）**：✅ 本方案——video_codec 实证可用。
- **手写 cc_toolchain_suite**：维护成本高且易错。Rejected。

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
- 测试资产根目录引入环境变量覆盖：`NETLIB_TEST_DATA_DIR`（默认沿用现仓库相对路径 `src/tests/certs/...`）；设备端运行时设为 `/data/local/tmp/cpp_network`（helper 自动拼 `certs/` 子目录）
- 宿主侧 Google Test 套件继续作为全量回归；Android 架构的 gtest 编译通过列为构建验收项之一

**Rationale**: 测试服务器留驻宿主使设备端二进制最小化（免 python/脚本分发），同时真实穿透 adb reverse 通道模拟应用联网路径；单二进制 + 明确退出码契合 make run 的自动化契约。

**Alternatives considered**: 把测试服务器整体跑在设备端（需 termux 类环境或移植 server 为 C++）——复杂度不成比例。Rejected。

## D6: make push / run 目标设计

**Decision**: 新增 `mk/android.mk`（沿用仓库 mk 模块化约定），目标契约详见 [contracts/make-targets.md](contracts/make-targets.md)：
- `make build-android`：构建 Android 架构下的 cpp_network 与设备端可执行程序（device_e2e、http_demo）
- `make push [DEVICE=serial]`：构建 → `adb [-s] push` 产物与 certs 到 `/data/local/tmp/cpp_network/`
- `make run [DEVICE=serial]`：前置 `adb reverse tcp:18080/tcp:18443`（可通过 PORT 变量扩展）→ `adb shell` 执行 → 解析 `EXIT:<code>` 透传退出码、输出实时回传
- 设备选择：DEVICE 显式优先；未指定时枚举 `adb devices`，0 台报错提示授权排查、≥2 台列出候选终止（US3 场景 3/4）

**Rationale**: adb reverse 让设备用 `127.0.0.1` 访问宿主服务，免除 IP/网段发现逻辑；退出码透传（`adb shell 'cmd; echo EXIT:$?'` 并 grep）保证 CI 化使用。mk/*.mk 模式与仓库既有 help/rules 注册机制一致。

**Alternatives considered**: gradle 封装工程——引入整套 Android 应用构建栈，与"开发回归闭环"定位不符（spec Assumptions 已排除打包上架）。Rejected。

## D7: 五平台矩阵更新方式

**Decision**: FR-010 通过三处证据落地：① `tools/platform_setup.sh` 输出 NDK 状态；② `specs/004-.../quickstart.md` 提供完整命令序列；③ 相关架构文档平台差异表将 Android 行更新为实测结论（附验收命令）。不改动 host 构建分支的行为与依赖图。

**Rationale**: 保持矩阵更新有据可查而非口头声明；host 侧零回归由 select() 结构性保证。

## D7 取证补充（2026-08-27，T010-T012 实测）

| 场景 | 耗时 |
|------|------|
| 增量（全缓存命中） | ~0.3s |
| `bazel clean` 后全量重建（repository/disk cache 外置生效） | **40.6s**（含 OpenSSL+curl 重新 configure/make） |
| 首次冷启动（无外置缓存，含源码下载+全量交叉编译） | Phase 2 期间实测分钟级 |

证据对应：SC-002（命令化产出物齐备）、SC-003 前半段（构建环节增量秒级）。异常路径守卫实测三分支：未设置→Error1+hint、过旧(25.2)→Error2、合规(28.2)→通过。

## D8 范围修订（2026-08-27，Phase 5 实施）：设备端验证改为外网 HTTPS

用户确认设备与宿主**不在同一网段**后拍板：若验证复杂，仅需测试外网 HTTPS 请求。据此：

1. `run` 移除"启动宿主 fixtures + adb reverse + 端口转发"链路（android_device.sh 简化，PORTS 变量废弃）。
2. `device_e2e` 改为**双模式**：默认 external（E1–E3：example.com GET/HEAD+大小写头读取/httpbin POST echo）；`NETLIB_TEST_MODE=local` 保留 S1–S7 编排（将来内网可达或推 server 上设备时启用）。
3. 自签/mTLS 行为一致性已由宿主侧证据覆盖：宿主 device_e2e S1–S7 曾实测 PASS 7/7；同套 gtest 持续回归。
4. 实施期修正一处编排 bug：`hostserial` 经命令替换调用时 `die` 的退出码被吞——新增 `select_device_or_exit` 显式 `|| exit $?` 传播。

验证矩阵（fake-adb 单设备仿真，宿主二进制代理执行）：全绿 PASS 3/3 exit0；故障注入(E1定向不可达)退出码2；多设备列出候选终止(exit1)；无设备 11ms 快速失败。

## D9 可移植性加固（2026-08-27，T019 实测）

**审计结论（android_device.sh 全量设备侧写入路径）**：
| 写入点 | 位置 | 收敛 |
|--------|------|------|
| push: `mkdir -p $DEVICE_DIR/{certs,tmp}` | genrule 外 adb shell | DEVICE_DIR 前缀校验后 ✓ |
| push: 二进制与 certs 上传 | 同上 | ✓ |
| run: 远端 envline 注入 `TMPDIR=$DEVICE_DIR/tmp` | 防未来 inline-PEM 兜底落盘写到不可写的 /tmp | ✓ |
| clean: `rm -rf '$DEVICE_DIR'` | 危险操作前置 prefix 校验 | ✓ |
| device_e2e external 模式（E1–E3） | 运行期零磁盘写入（无 inline PEM ⇒ 不触发 CachedPemPath；仅读 certs 于 local 模式） | ✓ |

**新增防护 `validate_device_dir`**：DEVICE_DIR 必须位于 `/data/local/tmp/**` 且不含 `..`，否则 exit 64 拒绝执行任何动作。实测：`/system`→64、含 `..` 穿越→64、空串回落默认目录、合规外网全链路回归 PASS 3/3。

**运行时零 root 断言**：全部写入收敛于 `/data/local/tmp/**`（adb shell 用户可写）；无系统分区访问点。

## D10 补记（Phase 6/7 实施）：local 模式编排 reinstated

用户确认真机验证自签/mTLS 必须 `adb reverse`（USB 回环天然免疫网段差异）。`android_device.sh run` 重启 `RUN_MODE=external|local` 双模式：local 启动宿主 fixtures(:18080/:18443/:18444) + 三条 reverse + 注入 `NETLIB_TEST_MODE=local` 与三个 BASE 变量，结束自动 teardown。同时修正资产根目录注入语义（`NETLIB_TEST_DATA_DIR=$DEVICE_DIR`，helper 内部拼 `certs/`），消除 certs/certs 双层路径。仿真验证：local PASS 7/7 exit0；external 回归 PASS 3/3 exit0。

## D11 T009 真机取证 & 一键证书验证目标（2026-08-27）

设备：be11（USB，serial an4009056e01d0d04，SELinux Permissive 无关紧要）。

1. **默认拒的真机负证据**：外网直连首次运行 3 场景全部 `kCertificateVerificationFailed`（我们源码 curl 显式 --without-ca-* 且 Android 无可消费默认信任库——印证 ADR-003/FR-003）。
2. **修复路径**：`stage_system_ca_bundle` 在设备端将 `/system/etc/security/cacerts/*` 合并为单 bundle 注入（FR-003 文档化模式，不改动库语义）。注意 cat 重定向发生在设备 shell 内部。
3. **`make verify-android` 一键闭环**：build-android → push → RUN_MODE=local(S1–S7 经 reverse) → external(E1–E3) 顺序执行，实测输出：`PASS 7/7` + `PASS 3/3`、整体退出码 0；期间发现并修复 stage 函数未接线的遗漏。
4. 实施中三次修正同一类问题（本地 vs 注入语义）：资产根目录统一为 helper 内拼 certs/ 的 TestAssetRoot 约定。

## D12 修订（Phase 7 后）：NDK 下限由 r26 放宽至 r25

r26+ 初始门槛源于文档基线假设，并非技术约束：构建链仅依赖统一布局下的交叉 clang/llvm-binutils 与 `-D__ANDROID_API__=24`（r19+ 均支持）。`tools/android_prereq.sh` 与 `platform_setup.sh` 的校验阈值调整为 **r25+**；发现逻辑不变（显式合法值优先，否则选最新可用版本）。已用本机 25.2.9519653 显式固定完成全链真机复验（见下节实测），28.x 仍为自动发现的默认选择。

**实测（2026-08-27，NDK 25.2.9519653 显式固定 + 真机 be11）**：`android_build` 42s（OpenSSL/curl 重编译），真机 `android_verify` 两段全绿：S1–S7 `PASS 7/7`、E1–E3 `PASS 3/3`、退出码 0。r25 与 r28 双工具链均验证可用，下限调整有效。
