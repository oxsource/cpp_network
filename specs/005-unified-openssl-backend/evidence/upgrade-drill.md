# 版本升级演练取证（specs/005 Phase 3 / T008-T009）

日期：2026-08-27；演练方式：同版本 no-op 触碰 `cpp_network_deps.bzl`（模拟"仅改版本标识与校验和"评审动作）→ `bazel clean` → 全量重建与回归

| 段落 | 命令 | 耗时 | 结果 |
|------|------|------|------|
| clean | `bazel clean` | <1s | 输出树清空；外置 repository/disk cache 保留 |
| 宿主回归 | `make verify` | **64s** | 构建成功 + 6/6 测试通过（log: drill_verify.log）|
| Android 重建 | `make android_build` | **49s** | 31 actions 从 disk cache 恢复重建（drill_android.log）|

**总耗时 ≈ 1 分 53 秒**（不含人工评审时间），远低于 SC-002 的 30 分钟上限。

## 发现与处置
- 无流程缺口：quickstart 步骤可逐字执行；未发现需要修正的文档漂移。
- NOTICE 可见性：NDK 自动解析的覆盖提示按预期出现在 stderr（T002 的 shell 行为），不影响耗时统计。
