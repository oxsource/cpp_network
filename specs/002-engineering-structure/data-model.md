# Data Model: 工程结构与依赖库组织

**Branch**: `002-engineering-structure` | **Date**: 2026-08-26 | **Spec**: [spec.md](spec.md)

## Entities

### Bazel 工作区（WORKSPACE）

- **Purpose**: 工程构建根；声明 workspace 名 `cpp_network`，经 `netlib_setup()` 引导外部依赖。
- **属性**:
  - `name: string` = `"cpp_network"` — workspace 唯一标识（内部依赖引用统一用 `@cpp_network//...` 前缀，如 `@cpp_network//src/public:netlib`）。
  - `setup: callable` = `netlib_setup()` — 幂等依赖引导。
- **关系**: 根 `BUILD.bazel` 提供 `alias //:netlib`；`examples/consumer_demo` 经 `local_repository` 引用。
- **校验**: 若 `netlib_setup` 重复调用不报错（`existing_rule` 守卫）；WORKSPACE 与 `.bazelversion`(6.5.0) 匹配。

### 平台定义（platforms/）

- **Purpose**: 五个命名平台，供 `select()` 与 `--config` 使用。
- **属性**（每个平台一个实体）:
  - `name: string` — macos_arm64 / macos_x86_64 / linux_x86_64 / linux_aarch64 / android_arm64。
  - `os_constraint` + `cpu_constraint` — 平台约束（`@platforms//os:*` + `@platforms//cpu:*`）。
  - 成对目标: `config_setting(<name>_setting)` + `platform(<name>)`。
- **关系**: `.bazelrc` 的 `--config=<platform>` → `--platforms=//platforms:<platform>`；TLS 后端 select 依赖平台约束。

### 依赖引导宏（netlib_deps.bzl）

- **Purpose**: 幂等拉取全部外部依赖的单一入口。
- **属性**:
  - 每个依赖: `name`、`version/commit`、`sha256`、`urls`。
- **关系**: 被 `WORKSPACE` 调用；拉取的仓库（@curl/@openssl/@googletest/@bazel_skylib）被 `third_party/` BUILD 引用。
- **校验**: 每个依赖用 `native.existing_rule(name)` 守卫；重复调用静默跳过。

### third_party 封装

- **Purpose**: 每个外部依赖的独立封装（BUILD + bzl），隔离版本与平台差异。
- **属性**:
  - `libcurl`: `libcurl_openssl`（USE_OPENSSL，全平台）目标。
  - `openssl`: `:ssl` + `:crypto`（全平台 TLS）。
  - `googletest`: re-export gtest/gtest_main。
  - `bazel_skylib`: re-export（或直接用 `@bazel_skylib//...`）。
- **关系**: `src/tls` 经 `netlib_select` 选择 libcurl 后端变体 + 对应 SSL 库。

### 公共库目标（//:netlib）

- **Purpose**: 工程对外交付的库目标。
- **属性**:
  - `:netlib` — cc_library（静态）。
  - `:netlib_shared` — cc_binary(linkshared=True, linkstatic=True)（共享）。
  - 导出宏 `NETLIB_API`；共享构建 `-DNETLIB_SHARED_LIBRARY`。
- **关系**: 根 `alias //:netlib` 指向它；`consumer_demo` 依赖它。
- **校验**: 共享库仅导出 `NETLIB_API` 符号（`nm` 检查，SC-004）。

### 便利构建入口（Makefile + mk/）

- **Purpose**: 开发者友好的命令映射层，转调 Bazel。
- **属性**:
  - `mk/rules.mk`: `register_module`/`register_target`/`register_alias` 宏（AOSP 风格）。
  - `mk/aliases.mk`: `build`/`test`/`verify`/`clean`。
  - `mk/help.mk`: `help`/`menu`。
- **关系**: 每个 Makefile 目标映射到 Bazel 命令。

### 平台设置产物（.user.bazelrc）

- **Purpose**: 本机平台配置，git-ignored。
- **属性**: `build --config=<本机平台>`。
- **关系**: `.bazelrc` 经 `try-import %workspace%/.user.bazelrc` 加载。
- **状态转换**: 生成（platform_setup.sh）→ 覆盖 `.bazelrc` 默认平台 → 被 gitignore。

## 状态转换

### 构建生命周期

```text
克隆 → 平台设置(生成 .user.bazelrc) → bazel build //... 
     → netlib_setup() 幂等拉取依赖 → 平台 select 解析 → 编译占位目标 → 产出 //:netlib
```

### 依赖拉取状态

```text
未拉取 → http_archive(下载+sha256校验) → 缓存 → (existing_rule 幂等：重复 setup 静默跳过)
```

## 校验规则汇总

- 依赖必须带 sha256（SC-006 可复现）。
- 平台名与 `.bazelrc` 别名一致。
- `netlib_deps.bzl` 幂等（重复调用不报错）。
- 共享库符号仅 `NETLIB_API`（SC-004）。
- `.user.bazelrc` 被 gitignore（FR-005）。
