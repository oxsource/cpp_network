# TLS Backend Selection Design (Build-Time)

**Branch**: `001-cpp-network-library` | **Date**: 2026-08-26

**对应需求**: FR-003（平台无关 TLS 抽象）、FR-016（不暴露后端细节）、FR-014（多平台构建）

**相关设计**: [bazel-platforms.md](bazel-platforms.md)、[tls-config.md](tls-config.md)、[host-openssl-build.md](host-openssl-build.md)、[android-boringssl-build.md](android-boringssl-build.md)

## Overview

TLS 由 libcurl 的 SSL 后端承担。在**构建时**通过 Bazel `select()` 选择：host（macOS/Linux）→ OpenSSL，Android → BoringSSL。库不提供运行时 `TlsAdapter` C++ 接口，TLS 细节完全封装在 libcurl 内部，公共 API 只暴露 `TlsConfig`（配置映射）。

## 决策依据

- research.md Decision 4：libcurl 已抽象多种 SSL 后端，复用其抽象可避免维护自定义 TLS 适配器。
- 自研 `TlsAdapter` 接口（手写 OpenSSL/BoringSSL 双后端）被否决：重复造轮子、接口维护成本高、易引入证书处理 bug。

## src/tls/ 布局

```text
src/tls/
├── BUILD.bazel          # select() 选择 SSL 后端并链接 libcurl
├── tls_config.h         # 公共 TlsConfig 类型（→ CURLOPT_SSL_* 映射见 tls-config.md）
├── tls_config.cc
└── internal/
    ├── ssl_backend.h    # 内部辅助：将 TlsConfig 应用到 CURLOPT（后端无关）
    └── ssl_backend.cc
```

`src/tls/BUILD.bazel`（核心选型逻辑）：

```python
load("//platforms:platforms.bzl", "netlib_select")

cc_library(
    name = "tls",
    srcs = ["tls_config.cc", "internal/ssl_backend.cc"],
    hdrs = ["tls_config.h", "internal/ssl_backend.h"],
    deps = netlib_select({
        # Android：BoringSSL 作为 libcurl 的 SSL 后端
        "@platforms//os:android": ["@boringssl//:boringssl", "@libcurl//:libcurl_boringssl"],
        # host（macOS/Linux）：OpenSSL 作为 libcurl 的 SSL 后端
        "//conditions:default": ["@openssl//:openssl", "@libcurl//:libcurl_openssl"],
    }),
    visibility = ["//visibility:public"],
)
```

## 关键机制

### 1. libcurl 按后端编译

- host：`@libcurl//:libcurl_openssl` — libcurl 以 `--with-openssl` 编译（见 host-openssl-build.md）。
- Android：`@libcurl//:libcurl_boringssl` — libcurl 以 BoringSSL 为 SSL 后端编译（见 android-boringssl-build.md）。
- 两个目标共享同一 `curl` 公共头（`curl/curl.h`），API 对上层一致，天然满足 FR-016。

### 2. TlsConfig → CURLOPT 映射（后端无关）

`ssl_backend.cc` 只调用 libcurl 的稳定 C API，不直接触碰 OpenSSL/BoringSSL 符号：

| TlsConfig 字段 | libcurl 选项 |
|----------------|--------------|
| `verify_mode == kVerifyPeer` | `CURLOPT_SSL_VERIFYPEER=1`, `CURLOPT_SSL_VERIFYHOST=2` |
| `verify_mode == kSkipVerification` | `CURLOPT_SSL_VERIFYPEER=0`, `CURLOPT_SSL_VERIFYHOST=0` |
| `ca_certificates`（内存 PEM） | `CURLOPT_CAINFO`（临时文件）或 `CURLOPT_CAINFO_BLOB`（curl ≥7.77 支持内存 blob） |
| `client_certificate` + key | `CURLOPT_SSLCERT`, `CURLOPT_SSLKEY` |
| `sni_hostname` | `CURLOPT_SSL_OPTIONS` / `CURLOPT_SNI_HOSTNAME`（按 curl 版本选择） |

### 3. 后端差异的隔离点

- **API 差异（不可见）**：libcurl 编译期已处理 OpenSSL vs BoringSSL 的 C API 差异（BoringSSL 移除的 deprecated 符号不影响 libcurl 编译，因为 libcurl 适配层已处理）。
- **CA 存储差异（可见配置差异）**：Android 需要显式指定系统 CA 或注入 CA bundle；host 默认系统 trust store。差异收敛在 `tls-config.md` 的"默认 CA 行为"中。
- **共享库符号**：若启用 `NETLIB_SHARED_LIBRARY`，`-fvisibility=hidden` 下仅 `NETLIB_API` 符号导出；curl/OpenSSL/BoringSSL 符号不导出（避免与宿主冲突）。

## 验证矩阵（构建期 gate）

| 平台 | SSL 后端 | 验证命令 |
|------|----------|----------|
| macOS arm64 | OpenSSL | `bazel build --config=macos_arm64 //src/tls:tls` |
| Linux x86_64 | OpenSSL | `bazel build --config=linux_x86_64 //src/tls:tls` |
| Android arm64 | BoringSSL | `bazel build --config=android_arm64 //src/tls:tls` |

构建通过即证明 `select()` 分支与后端依赖解析正确。

## 边界与约束

- 不做运行时后端切换（一个平台固定一个后端）。
- 不暴露 OpenSSL/BoringSSL 头文件到公共 API。
- 自定义 CA 的内存注入依赖 curl ≥7.77（`CURLOPT_CAINFO_BLOB`）；若降版本，回退临时文件方案。

## 评审要点

1. `select()` 是否覆盖 android + 默认兜底两个分支？
2. `libcurl_openssl` / `libcurl_boringssl` 两个依赖目标的 API 兼容性（同一 `curl/curl.h`）？
3. 共享库下 SSL 符号是否被隐藏（`-fvisibility=hidden`）？
4. Android CA 默认行为是否有明确文档（tls-config.md）？
