# ADR-001: 同步阻塞 API（库内无异步抽象）

**Status**: Accepted
**Date**: 2026-08-26
**决策依据文档**: [research.md](../../../specs/001-cpp-network-library/research.md) Decision 1

## Context

初稿设计为"完全异步 promise-based API"（Promise/Executor/WatchFd）。评审发现 C++ 缺 async/await 语法糖，裸 Promise 链模板复杂、体验差；且"线程调度、事件、流程"按用户要求应在库外实现。需重新决定库的 API 形态。

## Decision

库对外提供**同步阻塞 API**：`HttpClient::Send(HttpRequest)` 阻塞调用线程，直接返回 `Result<HttpResponse>`。库内**不实现**任何 Promise、协程、Executor、WatchFd、事件循环、线程调度。异步与流程编排由上层用线程池/协程/事件循环包装。

内部用共享 `CURLM*` + `curl_multi_poll` 实现连接池复用与多线程并发（见 sync-engine.md）。

## Alternatives Considered

| 备选方案 | 被否决原因 |
|----------|-----------|
| Promise-based（初稿） | C++ 无 async/await 语法糖，裸 Promise 链复杂；事件/流程应外置 |
| C++20 协程 | 需升级语言/NDK；仍是库内实现事件流程 |
| 异步回调 | 仍是库内回调流程，与"仅基础请求"冲突 |

## Consequences

- 公共 API 用 `Result<T>`（不抛异常），错误经 `Error` 码。
- 连接池复用经共享 CURLM + 锁串行化；并发吞吐受锁限制（polish 可升级 per-host 锁）。
- 重试、取消、超时累计由上层实现；库只做单次传输。
- contracts/quickstart 同步更新为同步形态。
