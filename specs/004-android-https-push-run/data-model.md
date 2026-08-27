# Data Model: Android HTTPS 支持与一键设备部署运行

**Branch**: `004-android-https-push-run` | **Date**: 2026-08-27

本特性不引入持久化数据域；以下为构建/部署闭环中的工作对象与状态模型。

## Entity 1: BuildArtifact（构建产物）

| 属性 | 类型 | 说明 |
|------|------|------|
| target | string | Bazel 目标名（如 `//src/tests:device_e2e`、`//src/examples/http_demo:http_demo`） |
| config | enum | `{host, android_arm64}` |
| arch | string | `arm64-v8a`（Android 侧固定；宿主侧取当前主机） |
| path | path | Bazel 输出树中的二进制/库路径（push 阶段解析） |
| state | enum | `{absent, built, pushed, stale}` |

**状态迁移**:

```text
absent → built        （build-android 成功）
built → stale         （源码/依赖变更后再次进入）
built → pushed        （make push 成功）
stale → absent? no    （stale 仅提示重跑 build-android，不自动删除产物）
```

**校验规则**: push 只接受 `state ∈ {built}` 且 config 为目标架构的产物；stale 产物 push 前必须重建。

## Entity 2: DeviceEntry（目标设备）

由平台工具枚举的连接设备清单条目。

| 属性 | 类型 | 说明 |
|------|------|------|
| serial | string | 设备序列号（工具链枚举输出的第一列） |
| state | enum | `{device, unauthorized, offline, absent}` |

**选择规则**（US3 场景 3/4）:

- 指定 `DEVICE=<serial>`：仅该序号参与；不存在或 state≠device → 报错终止
- 未指定：恰好 1 台 `device` 状态 → 直接使用；0 台 → 报错并给出授权/连接排查指引；≥2 台 → 列出全部候选后终止
- `unauthorized/offline` 条目不计入可用数量但需在报错中列出，便于区分"没插"与"没授权"

## Entity 3: ForwardChannel（本地服务转发通道）

设备端到宿主测试服务的端口映射（经 adb reverse 建立）。

| 属性 | 类型 | 说明 |
|------|------|------|
| device_port | int | 设备侧端口（默认 18080 HTTP / 18443 TLS） |
| host_port | int | 宿主侧端口（默认与 device_port 相同） |
| service | enum | `{http_test_server, tls_test_server}` |

**生命周期**:

```text
setup    ：run 目标启动前，为清单内每个映射执行 reverse 建立（幂等，重复执行覆盖旧映射）
active   ：宿主服务已监听 + reverse 已注册；e2e 通过 127.0.0.1:<device_port> 访问
teardown ：进程结束或目标退出后撤销 reverse（失败时尽力而为，不阻塞退出码上报）
failure  ：宿主端口被占用 → run 启动即失败并报告占用端口与改用变量（FR/US3 边界情况）
```

## Entity 4: OnDeviceCheckScenario（设备端检查场景）

device_e2e 内置的编号场景，复用 `src/tests/certs/` 资产。

| id | 场景 | 期望结果 |
|----|------|----------|
| S1 | 默认配置访问自签 TLS 服务（CA 注入为空） | 失败：证书校验失败类错误码 |
| S2 | 注入 CA 文件路径访问同一服务 | 200，返回预期 body |
| S3 | 内存 PEM 形态注入 CA 访问同一服务 | 200 |
| S4 | mTLS：无客户端证书 | 服务端拒绝（非 200 或握手错误，与服务端行为一致） |
| S5 | mTLS：携带正确客户端证书+私钥 | 200 |
| S6 | 跳过校验模式访问自签服务 | 200 |
| S7 | HTTP 基线（GET /notfound 场景） | 404 + 自定义头可读取 |

**聚合规则**: 全部通过 → 退出码 0；任一失败 → 以第一个失败场景的序号 +1 作为退出码（≥1，便于日志定位），其余场景仍继续执行以一次运行收集全量结果。
