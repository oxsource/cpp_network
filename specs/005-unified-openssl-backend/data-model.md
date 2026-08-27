# Data Model: 全平台统一 OpenSSL TLS 后端

**Branch**: `005-unified-openssl-backend` | **Date**: 2026-08-27

延续 specs/004 数据模型（BuildArtifact / DeviceEntry / ForwardChannel 已废止项 / OnDeviceCheckScenario），本特性新增与修订：

## Entity 1: TLSCapabilityMatrix（新增，文档性实体）

全平台能力矩阵的唯一事实源，落于 `docs/architecture/tls-backend-selection.md`。

| 属性 | 值域 |
|------|------|
| platform | `{android_arm64, macos_arm64, macos_x86_64, linux_x86_64, linux_aarch64}` |
| backend | 统一常量：源码构建 OpenSSL+curl（pin 见 cpp_network_deps.bzl） |
| trust_anchor | `user-injected`（所有平台一致；Android 上建议合并系统目录） |
| verify_level | `runtime-verified` / `build-only` |
| evidence | 指向 specs/005 验证记录的锚点 |

**校验规则**: `verify_level=runtime-verified` 必须附带可追溯证据锚点；矩阵中不允许出现未标注等级的平台行。

## Entity 2: BuildArtifact.config（修订）

原值域 `{host, android_arm64}` 细化：

```text
{macos_arm64, macos_x86_64, linux_x86_64, linux_aarch64, android_arm64}
```

**规则变化**: 所有 config 的 TLS 组成统一为"源码构建静态库"；`stale → built` 迁移在任一配置下互不影响（Bazel 配置隔离保证）。

## Entity 3: SymbolExportReport（新增，验证产出）

共享形态产物的一次符号审计结果。

| 属性 | 说明 |
|------|------|
| artifact | 审计对象（libcpp_network.so 等） |
| tool | 审计命令约定（nm -gU 等） |
| third_party_hits | 断言必须为 0 的计数项 |
| public_api_hits | 公开 API 符号计数（>0 即正常） |

**校验规则**: `third_party_hits == 0` 为 SC-004 通过条件；报告作为 Phase 7 文档矩阵的证据附件。

## 状态迁移图（BuildArtifact 跨平台语义修订后不变式）

```text
同一配置内迁移规则与 specs/004 一致；
跨配置交互约束：任何时刻不存在"部分平台新版本 + 部分平台旧版本"
的同一次升级发布——升级以全平台成功为提交条件（FR/Edge case 对应）。
```
