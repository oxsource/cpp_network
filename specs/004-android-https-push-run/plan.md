# Implementation Plan: Android HTTPS 支持与一键设备部署运行

**Branch**: `004-android-https-push-run` | **Date**: 2026-08-27 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `/specs/004-android-https-push-run/spec.md`

## Summary

在 Android 平台补齐 HTTPS 能力：源码交叉编译 OpenSSL 3.x LTS（已锁定版本）+ libcurl 并静态链接进 netlib，公共 API 保持平台无关；同时提供 `make push` / `make run` 等设备部署闭环目标——把设备端可执行程序推送到已连接的 Android 设备，经 `adb reverse` 访问宿主机本地测试服务，实时回传输出并透传退出码，使「改代码 → 构建 → push → run → 出结果」成为分钟级日常回归手段。

## Technical Context

**Language/Version**: C++17；构建系统 Bazel 6.5.0（workspace 主导，rules_foreign_cc 承载第三方 configure/make 项目）；NDK r26+（android_ndk_repository 接入）

**Primary Dependencies**: OpenSSL 3.0.13 LTS（`third_party/openssl` 已 pin，Android 分支源码交叉编译、静态链接）；libcurl 8.7.1（`third_party/libcurl` 已 pin，Android 分支以 `--with-openssl` 源码构建、协议裁剪为 HTTP/HTTPS）；ADB（宿主侧设备通道，非库依赖）

**Storage**: N/A — 无持久存储。设备端产物落 `/data/local/tmp/cpp_network/`（shell 可写私有目录）

**Testing**: 宿主侧既有 Google Test 全量回归不变；新增 Android 架构的 gtest 编译验证 + 新增设备端 e2e 检查程序（四类 HTTPS 场景 + HTTP 基线），测试服务器与证书仍驻留宿主、经端口反向转发供设备访问

**Target Platform**: host macOS arm64（现有验收基线）+ Android arm64-v8a 真机/模拟器（本特性验收平台）；Linux/Windows/macOS x86_64 宿主尽力兼容

**Project Type**: C++ 库 + 构建/部署工具链增强

**Performance Goals**: 增量闭环（单文件改动 → push+run 出结果）≤ 2 分钟（SC-003）；设备端测试结果等待不静默超时

**Constraints**: 设备免 root；公共头零改动（FR-002）；既有 macOS/Linux 行为零回归（FR-010）；OpenSSL/curl 版本锁定不得漂移

**Scale/Scope**: 单架构交付（arm64-v8a），x86_64 模拟器架构预留但不在验收范围；不涉及 APK 打包/签名/上架流程

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

Constitution 为模板、无已定义原则，无违规可评估。Gate: PASS。

## Project Structure

### Documentation (this feature)

```text
specs/004-android-https-push-run/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output
│   ├── make-targets.md      # make build-android/push/run 目标契约
│   └── device-test-contract.md  # 设备端 e2e 场景/目录布局/端口转发契约
└── tasks.md             # Phase 2 output (/speckit.tasks)
```

### Source Code (repository root)

```text
platforms/BUILD                     # android_arm64 platform 已就绪（无需改动）
tools/platform_setup.sh             # 增加 NDK 检测提示（ANDROID_NDK_HOME）
WORKSPACE                           # 注册 rules_foreign_cc + android_ndk_repository
third_party/openssl/
├── openssl.bzl                     # 版本已 pin（复用）
└── BUILD.bazel                     # android 分支：configure_make 产物 ssl/crypto；
                                    #   host 分支保持系统占位（select 切换）
third_party/libcurl/
├── libcurl.bzl                     # 版本已 pin（复用）
└── BUILD.bazel                     # android 分支：autotools --with-openssl，
                                    #   协议裁剪 http/https；host 继续系统 -lcurl
src/http/BUILD.bazel                # linkopts/select：host=-lcurl；android=@libcurl//:curl
mk/android.mk                       # build-android / push / run / clean-device 目标
Makefile                            # include mk/android.mk
src/tests/device_e2e.cc             # 设备端 e2e 检查程序（自包含场景编排）
src/tests/BUILD.bazel               # device_e2e 目标（cc_binary，android 可执行）
docs/architecture/tls-config.md     # 补充 Android 平台差异落地状态
specs/004-.../contracts/*.md        # 见上
```

**Structure Decision**: 全部落在既有结构内——第三方源码构建进 `third_party/*` 的既有占位包，平台差异由 Bazel `select()` 在实现层切换；部署工具链进 Makefile 的模块化 `mk/android.mk`（沿用 AOSP 风格约定）；设备端检查程序放 `src/tests/` 与既有测试同域。公共 API 目录零变更。

## Complexity Tracking

N/A — Constitution 无违规，无需复杂度论证。
