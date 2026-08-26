# TLS Backend Selection Design（全平台 OpenSSL）

**Branch**: `001-cpp-network-library` | **Date**: 2026-08-26（修订：2026-08-26 全平台 OpenSSL）

> **状态（2026-08-26，spec 003 实现核对）**：决策方向（全平台 OpenSSL 经 libcurl、不暴露后端类型）已实现，但落地形态与本文布局不同——见下方"实际落地"节。

**对应需求**: FR-003（平台无关 TLS 抽象）、FR-016（不暴露后端细节）、FR-014（多平台构建）

**相关设计**: [bazel-platforms.md](bazel-platforms.md)、[tls-config.md](tls-config.md)、[host-openssl-build.md](host-openssl-build.md)

## Overview

TLS 由 libcurl 的 SSL 后端承担，**统一使用 OpenSSL 3.x LTS**（host macOS/Linux 与 Android 平台一致）。库不提供运行时 `TlsAdapter` C++ 接口，TLS 细节完全封装在 libcurl 内部，公共 API 只暴露 `TlsConfig`（配置映射）。

> **修订说明**：初稿为"host=OpenSSL / Android=BoringSSL"双后端 select() 方案。2026-08-26 用户决策**全平台统一 OpenSSL**，放弃 Android BoringSSL。理由：简化架构（无平台分支）、规避 BoringSSL 与 Bazel 6.5 的兼容问题。`android-boringssl-build.md` 已废弃（见其头部标注）。

## 决策依据

- research.md Decision 4（修订）：libcurl 已抽象多种 SSL 后端，复用其抽象可避免维护自定义 TLS 适配器；全平台统一 OpenSSL 免除构建时后端 select。
- 自研 `TlsAdapter` 接口（手写 OpenSSL/BoringSSL 双后端）被否决：重复造轮子、接口维护成本高、易引入证书处理 bug。

## 实际落地（spec 003）

与本文原布局的差异：

```text
src/tls/
├── BUILD.bazel          # cc_library "tls"：仅依赖公共头（Error/Result header-only）
└── tls.cc               # Tls::Validate() 校验（CA 互斥/mTLS 成对/SNI/PEM 形态）
src/http/detail/
├── curl_mapping.cc      # Tls → CURLOPT_SSL_* 映射（逐请求由引擎应用），
                         # 含 blob → 临时文件回退（MaterializePem）
src/public/include/http/
└── tls.h                # 公共类型：cpp_network::http::Tls / VerifyMode
```

- 无 `internal/ssl_backend.*`：映射直接内联在 HTTP 引擎的 curl_mapping 层（逐请求应用，且依赖 MaterializePem 等传输期辅助）。
- libcurl 以**系统库**形式经 `linkopts = ["-lcurl"]` 链接；`@openssl//:openssl`、`@libcurl//:libcurl_openssl` 尚为占位（见 host-openssl-build.md 状态横幅）。OpenSSL 不作为独立 Bazel 依赖出现。
- 验证矩阵中 macOS arm64 已实测通过（https_test）；Linux/Android 构建配置就绪待验证。

## src/tls/ 布局（001 原设计，未采用）

```text
src/tls/
├── BUILD.bazel          # 链接 libcurl（OpenSSL 后端），无平台 select
├── tls_config.h         # 公共 TlsConfig 类型
├── tls_config.cc
└── internal/
    ├── ssl_backend.h    # 内部辅助：将 TlsConfig 应用到 CURLOPT（OpenSSL 后端）
    └── ssl_backend.cc
```

`src/tls/BUILD.bazel`（全平台统一，无 select）：

```python
cc_library(
    name = "tls",
    srcs = ["tls_config.cc", "internal/ssl_backend.cc"],
    hdrs = ["tls_config.h", "internal/ssl_backend.h"],
    deps = [
        "@openssl//:openssl",
        "@libcurl//:libcurl_openssl",   # USE_OPENSSL，全平台
    ],
    visibility = ["//visibility:public"],
)
```

## 关键机制

### 1. libcurl 以 OpenSSL 后端编译（全平台）

- `@libcurl//:libcurl_openssl` — libcurl 以 `--with-openssl` 编译（见 host-openssl-build.md），host 与 Android 平台相同。
- 单一 `curl/curl.h` 公共头，API 对上层一致，天然满足 FR-016。

### 2. Tls → CURLOPT 映射（后端无关）

`curl_mapping.cc` 只调用 libcurl 的稳定 C API，不直接触碰 OpenSSL 符号（完整表见 tls-config.md）：

| Tls 字段 | libcurl 选项 |
|----------------|--------------|
| `verify_mode == kVerifyPeer` | `CURLOPT_SSL_VERIFYPEER=1`, `CURLOPT_SSL_VERIFYHOST=2` |
| `verify_mode == kSkipVerification` | `CURLOPT_SSL_VERIFYPEER=0`, `CURLOPT_SSL_VERIFYHOST=0` |
| `ca_file` / `ca_certificate`（内存 PEM） | `CURLOPT_CAINFO` / `CURLOPT_CAINFO_BLOB`（运行时 ≥7.77，失败回退临时文件） |
| `client_cert` + `client_key`（PEM 或路径） | `CURLOPT_SSLCERT(_BLOB)`, `CURLOPT_SSLKEY(_BLOB)`（运行时 blob ≥7.71，回退临时文件） |
| `sni` | `CURLOPT_SNI_HOSTNAME`（编译期 #ifdef 保护） |

### 3. 平台差异的隔离点

- **CA 存储差异（可见配置差异）**：Android 需要显式指定系统 CA 或注入 CA bundle；host 默认系统 trust store。差异收敛在 `tls-config.md` 的"默认 CA 行为"中。
- **共享库符号**：若启用 `NETLIB_SHARED_LIBRARY`，`-fvisibility=hidden` 下仅 `NETLIB_API` 符号导出；curl/OpenSSL 符号不导出（避免与宿主冲突）。

## 验证矩阵（构建期 gate）

| 平台 | SSL 后端 | 验证命令 |
|------|----------|----------|
| macOS arm64 | OpenSSL | `bazel build --config=macos_arm64 //src/tls:tls` |
| Linux x86_64 | OpenSSL | `bazel build --config=linux_x86_64 //src/tls:tls` |
| Android arm64 | OpenSSL | `bazel build --config=android_arm64 //src/tls:tls` |

构建通过即证明 OpenSSL 后端依赖解析正确。

## 边界与约束

- 不做运行时后端切换（全平台固定 OpenSSL）。
- 不暴露 OpenSSL 头文件到公共 API。
- 自定义 CA 的内存注入依赖 curl ≥7.77（`CURLOPT_CAINFO_BLOB`）；若降版本，回退临时文件方案。
- Android 上需确认 OpenSSL 3.x 的可交叉编译性（NDK 工具链），见 host-openssl-build.md 的扩展说明。

## 评审要点

1. 全平台是否统一走 OpenSSL（无任何 BoringSSL/select 残留）？
2. `@libcurl//:libcurl_openssl` 依赖目标的 API 兼容性（单一 `curl/curl.h`）？
3. 共享库下 SSL 符号是否被隐藏（`-fvisibility=hidden`）？
4. Android CA 默认行为是否有明确文档（tls-config.md）？
