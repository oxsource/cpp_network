# Contract: 设备端 e2e 测试

**Branch**: `004-android-https-push-run`

> **修订（2026-08-27）**：设备与开发宿主通常**不在同一网段**，`adb reverse` 拓扑依赖 USB 通道而非网络可达性虽可行，但经用户确认验证复杂度高。按用户决策改为**默认仅测试外网 HTTPS**（本地自签/mTLS 场景保留在宿主 gtest 覆盖；`NETLIB_TEST_MODE=local` 保留 S1–S7 编排以备将来内网可达时使用）。

## 双模式定义

| 模式 | 触发 | 场景 | 依赖 |
|------|------|------|------|
| **external**（默认） | 无需设置 | E1–E3（外网 HTTPS） | 设备自身互联网连通 |
| local-fixture | `NETLIB_TEST_MODE=local` | S1–S7（自签/mTLS/HTTP 基线） | 宿主 fixtures + `adb reverse` 可达 |

```text
external 模式（默认）：                local-fixture 模式（保留实现）：
设备 ──自身网络──> example.com          宿主 fixtures(:18080/:18443/:18444)
      https://httpbin.org/post            └─ adb reverse tcp:N ──> device_e2e
   （无需宿主参与、无端口转发）
```

## 运行环境约定

| 约定项 | 宿主运行 | 设备运行 |
|--------|----------|----------|
| 资产根目录 | 缺省沿用仓库相对路径 `src/tests/certs/...` | `NETLIB_TEST_DATA_DIR=/data/local/tmp/cpp_network/certs` |
| 外网基址 | `NETLIB_TEST_EXT_BASE`（默认 `https://example.com`），POST 固定 `https://httpbin.org/post` | 同 |
| 本地基址 | `NETLIB_TEST_HTTP_BASE` / `HTTPS_BASE` / `MTLS_BASE` 默认 `127.0.0.1:18080/18443/18444` | 经转发等价可达 |

`NETLIB_TEST_DATA_DIR` 需在读取证书的既有代码路径生效：读取 CA 文件 / 客户端证书与私钥路径；缺省值行为不变（宿主 gtest 无需设置）。代理环境注意：真实设备直连互联网，不应设置 `*_proxy` 环境变量注入到远端命令。

## 场景清单（退出码 = 失败场景序号 +1）

**E1–E3（external，默认）** — 见 [data-model.md](../data-model.md) 与下述补充：

- E1 `https://example.com` GET：200 且 body 含 "Example Domain"（证书校验、DNS、TLS1.2+ 全链路）
- E2 HEAD + `GetHeader("content-TYPE")` 大小写不敏感读取 text/html
- E3 `https://httpbin.org/post` JSON POST：200 且回显包含发送字段
- 哨兵协议：远端 shell 末行输出 `__NETLIB_EXIT__:<code>`，宿主剥离后作为退出码并打印 `[device-exit: <code>]`

**S1–S7（local-fixture，可选保留）**：

- S2/S4/S5 文件形态证书路径；S3 内存 PEM；S6 kSkipVerification
- 每场景独立 Client，失败不中断；末尾 `PASS <n>/<total>`

## 对既有测试资产的影响

- `test_server.py` / `test_tls_server.py` / `certs/*` 内容与监听端口不变，仍由宿主 gtest 使用
- 宿主侧 gtest 全量回归保持现状且行为不变；唯一新增是资产根目录的环境变量覆写点