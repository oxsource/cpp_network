# TLS Backend Selection Design（后端跟随平台）

**Branch**: `001-cpp-network-library` | **Date**: 2026-08-26（第二次修订：2026-08-26 改为"后端跟随平台"，详见 ADR-003）

> **状态（2026-08-26 第二次修订）**：ADR-003 已由"全平台统一 OpenSSL 源码构建"修订为"**TLS 后端跟随平台，验收标准 = HTTPS + 自定义证书能力**"。本文原"全平台 OpenSSL"章节保留为历史设计，现行策略以"实际落地"节为准。

**对应需求**: FR-003（平台无关 TLS 抽象）、FR-016（不暴露后端细节）、FR-014（多平台构建）

**相关设计**: [ADR-003](adr/adr-003-tls-buildtime-select.md)、[bazel-platforms.md](bazel-platforms.md)、[tls-config.md](tls-config.md)、[host-openssl-build.md](host-openssl-build.md)

## Overview

TLS 由 libcurl 的 SSL 后端承担，**后端跟随各平台惯例、不做统一要求**：验收标准是各平台经 libcurl 支持 HTTPS 与自定义证书（CA 注入/mTLS/skip），而非绑定特定后端。库不提供运行时 `TlsAdapter` C++ 接口，公共 API 只暴露 `Tls` 配置值类型。

> **修订说明**：初稿为"host=OpenSSL / Android=BoringSSL"双后端 select()；第一次修订改为全平台统一 OpenSSL 源码构建；第二次修订（2026-08-26）确认用户需求不含"统一 OpenSSL"，且系统 libcurl 在 host 上已满足全部能力——源码构建降级为可选项。`android-boringssl-build.md` 已废弃。

## 决策依据

- libcurl 不自带 TLS 后端，但绑定任一发行版自带的后端即可支持 HTTPS（macOS 系统 curl = SecureTransport，Linux 发行版普遍 = OpenSSL）。
- 自研 `TlsAdapter` 接口被否决：重复造轮子、接口维护成本高、易引入证书处理 bug。
- 全平台统一 OpenSSL 源码构建被降级：工程量大、在无 Android 真机阶段不可验证，且用户需求不要求后端统一。

## 实际落地（spec 003）

与本文原布局的差异：

```text
src/tls/
├── BUILD.bazel          # cc_library "tls": public headers only (Error/Result header-only)
└── tls.cc               # Tls::Validate() checks (CA exclusivity/mTLS pairing/SNI/PEM form)
src/http/detail/
├── curl_mapping.cc      # Tls → CURLOPT_SSL_* mapping (applied per-request by the engine),
                         # incl. blob → cached-file fallback (CachedPemPath)
src/public/include/http/
└── tls.h                # Public types: cpp_network::http::Tls / VerifyMode
```

- 无 `internal/ssl_backend.*`：映射直接内联在 HTTP 引擎的 curl_mapping 层（逐请求应用，且依赖 CachedPemPath 等传输期辅助）。
- libcurl 以**系统库**形式经 `linkopts = ["-lcurl"]` 链接；`@openssl//:openssl`、`@libcurl//:libcurl_openssl` 尚为占位（见 host-openssl-build.md 状态横幅）。OpenSSL 不作为独立 Bazel 依赖出现。
- 验证矩阵中 macOS arm64 与 **Android arm64 均已实测通过（2026-08-27，specs/004）**：macOS 走系统 libcurl + https_test；Android 为源码交叉编译 OpenSSL 3.0.13 + curl 8.7.1 静态链接（真机/模拟器产物 `ELF aarch64`），Linux 构建配置就绪待验证。

## src/tls/ 布局（001 原设计，未采用）

```text
src/tls/
├── BUILD.bazel          # Links libcurl (OpenSSL backend), no platform select
├── tls_config.h         # Public TlsConfig type
├── tls_config.cc
└── internal/
    ├── ssl_backend.h    # Internal helper: applies TlsConfig to CURLOPTs (OpenSSL backend)
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
        "@libcurl//:libcurl_openssl",   # USE_OPENSSL, all platforms
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
| `ca_file` / `ca_pem`（内存 PEM） | `CURLOPT_CAINFO` / `CURLOPT_CAINFO_BLOB`（运行时 ≥7.77，失败回退临时文件） |
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
