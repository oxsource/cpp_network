# Research: 全平台统一 OpenSSL TLS 后端

**Branch**: `005-unified-openssl-backend` | **Date**: 2026-08-27

延续 specs/004 已验证事实（OpenSSL/curl 交叉编译链、单一 genrule、`-fvisibility=hidden` 注入点），本文档只解决"扩展到宿主"新增的开放问题。

## D1: 宿主模式下的编译工具链选择

**Decision**: host 分支不引入交叉参数，直接使用动作沙箱 PATH 中的系统 `cc/clang` 与 `ar/ranlib`；OpenSSL `Configure` 目标按 `uname -s -m` 映射（`darwin64-arm64-cc` / `darwin64-x86_64-cc` / `linux-x86_64` / `linux-aarch64`）；curl 侧去除 `--host=`，其余裁剪项与 Android 分支完全一致。

**Rationale**:
- 可复现性的关键在 **pin + 裁剪集一致**，而非编译器绝对同一——Bazel 动作沙箱已隔离环境污染；强行固定编译器路径会把跨机器差异转移成新的维护面
- 系统默认 ar 在 macOS 为 BSD 格式，规避 video_codec 教训中"GNU 归档 + ld64 拒绝"的已知陷阱；为防御性起见显式导出 `AR=/usr/bin/ar RANLIB=/usr/bin/ranlib`（macOS）/ 保持系统值（Linux）
- 与 Android 分支共用同一脚本文件，仅 MODE 分叉，防止两份漂移

**Alternatives considered**:
- 固定传递 Bazel 默认 cc_toolchain 的具体 clang 绝对路径：分析期不可知（需 repo rule 探测），复杂度不成比例。Rejected。
- 让 curl/OpenSSL 使用 CMake 构建家族：无收益，徒增第二条构建路径。Rejected。

## D2: 共享库符号严格隐藏的实现组合

**Decision**: 三层组合：
1. 第三方归档以 `-fvisibility=hidden -fPIC` 编译（对象层隐藏 OpenSSL/curl 全部实现符号）
2. 本库各 target 延续既有 `CPP_NETWORK_HTTP_*` 导出宏机制（export.h 已定义）
3. Linux 链接侧追加 `-Wl,--exclude-libs,ALL` 兜底（静态归档内部符号不再参与动态导出决策）；macOS 上对象层隐藏已足够，不加平台特有 ld 标志

**Rationale**: 对象层方案同时覆盖 macOS/Linux 且无需维护链接器版本脚本；`--exclude-libs,ALL` 是 GCC/lld 支持的标准项，作为纵深防御成本极低。

**Verification hook**: 交付物包含一次 `nm -gU libcpp_network.so` 抽查记录：断言无 `SSL_* / EVP_* / Curl_*` 外泄。

**Alternatives considered**: 维护 linker version script（精确但双平台语法差异+长期维护）。Rejected。

## D3: 内存证书注入通道的平台怪癖消除

**Decision**: 统一后所有平台的信任锚注入走传输层原生能力通道（pin 版本 ≥7.77 的内存 blob 能力直接生效）；specs/004 引入的临时文件兜底保持代码可用但转入"历史兼容"定位（公共 API 不删），测试矩阵的 S3 场景不再依赖它。

**Rationale**: BLOB 运行时支持取决于我们自建的传输层版本与后端组合（均已锁定满足），平台怪癖源（macOS 系统 curl 运行时拒绝）随系统链接路径移除而消失。

**Alternatives considered**: 同步删除兜底函数。Rejected——保留可服务未来低版本宿主场景与第三方特殊配置。

## D4: Linux 无本机运行条件的验证等级

**Decision**: Linux x86_64/aarch64 按"构建级验证"等级交付并在矩阵标注；配套提供一条可直接在目标机执行的单一命令自检说明（复用 device_e2e 的 external 模式即可运行三场景，无需任何 fixture）。不在本特性内宣称运行级通过。

**Rationale**: 诚实标注优于模糊声明；external 模式恰好为"拿到任意一台 Linux 机器就能补齐运行证据"留了零门槛通道。

**Alternatives considered**: CI 远程矩阵申请（超出当前仓库基础设施现状）。Deferred。

## D5: select 收敛后的回退策略

**Decision**: 直接翻转默认并物理删除 `-lcurl` 分支；回退手段 = 单次 git revert。为降低"翻车发现晚"的风险，翻转提交前先在本机完成全量 gtest + 双模式 e2e 取证（Phase A/B 顺序执行保证）。

**Rationale**: 保留双分支等于永久背负两套行为语义，与"统一后端"初衷冲突；pre-flip 取证把回退概率压到最低。

**Alternatives considered**: opt-in 配置渐进式。用户已明确选择直接切换。Superseded by user decision。
