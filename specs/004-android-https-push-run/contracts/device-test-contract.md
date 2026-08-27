# Contract: 设备端 e2e 测试

**Branch**: `004-android-https-push-run`

## 组成与职责划分

```text
宿主（host）                     通道                    设备（Android）
─────────────────────           ─────                   ─────────────────
test_server.py     :18080   ──┐
                              ├─ adb reverse tcp:N tcp:N ── device_e2e
test_tls_server.py :18443/44┘                             （自包含场景编排，
certs/*.pem（原文件保留）                                    退出码聚合）
```

- 测试服务器与证书的启动/存留始终在宿主侧；`run` 目标负责拉起并等待就绪（复用现有 `curl --max-time` 探活逻辑）
- `device_e2e` 为纯客户端程序，依赖公共 API 与 certs 文件目录，无任何 Bazel/仓库结构假设

## 运行环境约定

| 约定项 | 宿主运行 | 设备运行 |
|--------|----------|----------|
| 资产根目录 | 缺省沿用仓库相对路径 `src/tests/certs/...` | `NETLIB_TEST_DATA_DIR=/data/local/tmp/cpp_network/certs` |
| 服务基址 | `http(s)://127.0.0.1:<port>`（本机直连） | 同形式地址，经反向转发落到宿主服务 |
| TLS 端口 | 18443（自签服务）、18444（mTLS 要求客户端证书） | 经转发等价可达 |

`NETLIB_TEST_DATA_DIR` 需在读取证书的两处既有代码路径生效：读取 CA 文件 / 客户端证书与私钥路径；缺省值行为不变（宿主 gtest 无需设置）。

## 场景清单（退出码 = 失败场景序号 +1）

见 [data-model.md](../data-model.md) Entity 4（S1–S7）。实现要点：

- S2/S4/S5 使用文件形态证书路径（验证 Android 上 `SetCaFile`、`SetCertificate` 的文件路径链路）
- S3 使用内存 PEM 注入（覆盖 `SetCaPem` + blob/CachedPemPath 链路在 Android 分支的行为）
- S6 使用 `VerifyMode::kSkipVerification`
- 每个场景独立连接，不复用 Client，失败不中断后续场景；末尾输出 `PASS <n>/<total>` 或首个失败摘要

## 对既有测试资产的影响

- `test_server.py` / `test_tls_server.py` / `certs/*` 内容与监听端口不变
- 宿主侧 gtest 全量回归保持现状且行为不变；唯一新增是资产根目录的环境变量覆写点
