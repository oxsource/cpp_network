# Connection Pool Tuning Design

**Branch**: `001-cpp-network-library` | **Date**: 2026-08-26

**对应需求**: FR-012（连接池复用）、SC-004（100 并发连接）、SC-007（流式内存）

**用户故事**: US3 (P2) — Configure Network Client Settings

**相关设计**: [network-config.md](network-config.md)、[http-config-mapping.md](http-config-mapping.md)、[data-model.md](../../specs/001-cpp-network-library/data-model.md)

## Overview

连接池由 **libcurl 内部管理**（`CURLM*`），库只暴露调优旋钮：`max_connections_per_host`（单 host 并发上限）与 `keep_alive`（连接复用窗口）。库不实现自研连接池。

## 决策依据

- research.md Decision 2：libcurl 自带连接复用（`CURLMOPT_MAX_HOST_CONNECTIONS`/`CURLMOPT_MAXCONNECTS`），自研连接池为重复劳动。
- data-model.md：`ConnectionPool` 实体已标注"委托 libcurl，状态机内部管理"。

## 调优旋钮 → libcurl 映射

| NetworkConfig 字段 | libcurl 选项 | 默认 | 语义 |
|--------------------|--------------|------|------|
| `max_connections_per_host` | `CURLMOPT_MAX_HOST_CONNECTIONS` | 5 | 同一 host 的**并发**连接上限；超出排队等待空闲 |
| `keep_alive` | `CURLMOPT_MAXCONNECTS`（缓存总数）+ libcurl 内部 keep-alive 超时 | 120s | 空闲连接被复用的时间窗口；超过则关闭 |

### 实现位置

```cpp
// SyncEngine 创建共享 CURLM* 时应用一次（http-config-mapping.md 的 multi 级选项）
curl_multi_setopt(multi_, CURLMOPT_MAX_HOST_CONNECTIONS, config_.max_connections_per_host);
curl_multi_setopt(multi_, CURLMOPT_MAXCONNECTS, 缓存总数(由 keep_alive 推导或直接设值));
```

**注意**：`CURLMOPT_MAXCONNECTS` 控制的是缓存连接总数上限，与 `keep_alive` 时长正交。v1 将 `keep_alive` 映射为：
- `CURLMOPT_MAXCONNECTS = max_connections_per_host * 预期 host 数`（简单启发式，详见评审要点 1）；或
- 若无需精确控制，仅设置 `CURLMOPT_MAX_HOST_CONNECTIONS`，空闲回收交给 libcurl 默认（`curl_multi_timeout` 驱动的 keep-alive）。

## SC-004（100 并发）达成路径

- `max_connections_per_host` 默认 5，但可上调：`SetMaxConnectionsPerHost(100)` → `CURLMOPT_MAX_HOST_CONNECTIONS=100`。
- 并发承载：多线程并发 `Send` 经 mutex 串行化进共享 CURLM，`curl_multi_poll` 在锁内驱动所有 easy 的 IO（sync-engine.md）。
- SC-004 验证：100 并发请求同一 host 需 `max_connections_per_host ≥ 100`（若希望并行而非排队），或接受排队（连接池语义）。

## 连接生命周期（内部）

```text
Idle ↔ In-Use → Closed
  ├─ keep-alive 到期（空闲超时）→ libcurl 关闭
  ├─ 错误/重置 → 关闭
  └─ SyncEngine 析构 → curl_multi_cleanup 全清
```

## 与错误语义联动

- 连接池耗尽（并发达 `max_connections_per_host` 上限且排队超时）→ libcurl 报错 → 映射：
  - 若可等待，请求排队（默认行为，无显式错误）；
  - 若显式超时（`CURLMOPT_MAXCONNECTS` 相关或传输超时）→ `kConnectionPoolExhausted` 或对应超时错误（core-error.md 已预留）。

## 边界与约束

- 不做自研 LRU/连接驱逐（委托 libcurl）。
- 不做 per-host 差异化池配置（全局旋钮）。
- `keep_alive` 精确映射依赖 libcurl 版本行为；v1 以文档化"近似语义"交付，精确控制留 polish。

## 评审要点

1. `keep_alive` → `CURLMOPT_MAXCONNECTS` 的启发式映射是否足够，还是 v1 只暴露 `max_connections_per_host` 更稳妥？
2. SC-004（100 并发）通过配置 `max_connections_per_host≥100` 达成，是否需要在 contracts/quickstart 标注？
3. 连接池耗尽时的错误映射（排队 vs 超时）是否清晰？
