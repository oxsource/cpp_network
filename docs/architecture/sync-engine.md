# Synchronous Engine Design (共享 CURLM + curl_multi_poll)

**Branch**: `001-cpp-network-library` | **Date**: 2026-08-26（同步重构版）

**对应需求**: FR-012（连接池复用）、FR-011（并发安全）、SC-004（100 并发）

**相关设计**: [http-transfer-lifecycle.md](http-transfer-lifecycle.md)、[http-config-mapping.md](http-config-mapping.md)、[core-error.md](core-error.md)

## Overview

设计库的同步传输引擎：内部维护一个**共享的 `CURLM*`**（mutex 保护）。每次 `Send` 在调用线程**阻塞**，通过 `curl_multi_poll` 等待该请求完成。多线程并发调用经锁串行化进入 multi，从而在同步 API 下**复用连接池**（keep-alive）。无回调、无事件循环、无 Promise/Executor。

**取代**：原 `core-executor.md` / `core-promise.md` / `curl-engine-bridge.md`（异步事件桥接设计，已随同步重构废弃）。

## 为什么不用 curl_easy_perform

- `curl_easy_perform` 最简单（同步阻塞），但**每次调用新建 TCP/TLS 连接，不复用**，违反 FR-012 与 SC-004（100 并发 + 连接复用）。
- 共享 `CURLM*` + `curl_multi_poll`：请求加入 multi 后阻塞轮询，multi 内部跨请求复用空闲连接。这是**同步 + 连接复用**的正确组合。

## 核心组件

```cpp
// src/http/engine.h（内部，非公共 API）
namespace netlib::internal {

class SyncEngine {
 public:
  explicit SyncEngine(const NetworkConfig& config);
  ~SyncEngine();   // curl_multi_cleanup

  // 同步执行单个请求，阻塞调用线程直至完成。
  // 返回 Result<HttpResponse>（成功）或 Result<Error>（失败）。
  Result<HttpResponse> Send(const HttpRequest& req);

  void Close();    // 关闭连接池（清空 multi）

 private:
  mutable std::mutex mu_;      // 保护 multi_ 与传输集
  CURLM* multi_;
  NetworkConfig config_;
  bool closed_ = false;
};

}  // namespace netlib::internal
```

## Send 同步流程

```cpp
Result<HttpResponse> SyncEngine::Send(const HttpRequest& req) {
  // 1. 校验 HttpRequest（见 http-request.md）→ 失败返回 kInvalidArgument
  // 2. 加锁 mu_
  // 3. 创建 CURL easy，应用选项（config + request 覆盖，见 http-config-mapping.md）
  // 4. curl_multi_add_handle(multi_, easy)
  // 5. 循环阻塞：
  //      curl_multi_poll(multi_, nullptr, 0, 超时ms, &numfds);
  //      curl_multi_perform(multi_, &running);
  //      检查 curl_multi_info_read 是否本 easy 完成（CURLMSG_DONE）
  // 6. 完成 → 提取响应/错误（见 http-response.md / core-error.md）
  //    curl_multi_remove_handle → curl_easy_cleanup
  // 7. 解锁 mu_
  // 8. 返回 Result<HttpResponse>
}
```

关键点：
- **单次传输超时**：`curl_multi_poll` 的 timeout 参数取 `total_timeout` 剩余（若有），超时后检查传输是否卡死，以 `kTotalTimeout` 返回（见 http-transfer-lifecycle.md）。
- **多请求并发**：加锁期间只有当前线程在驱动 multi；**其他请求仍可能同时被 curl_multi_poll 处理**（同锁内所有 easy 的 IO 都被推进），从而共享连接池。这正是"同步 API + 连接复用"的关键收益。
- **锁粒度**：整个传输期间持锁。短请求（<100ms 常见）锁竞争可接受；SC-004 100 并发的吞吐由锁串行化 + 连接池复用折中（评审要点 1）。

## 并发与线程安全

- `SyncEngine::Send` 可从任意线程并发调用（`HttpClient` 线程安全）。
- 同一时刻仅一个线程驱动 multi（互斥）；其余线程阻塞等待锁。
- `Close()`：加锁，`curl_multi_cleanup`，将 `closed_` 置真；此后 `Send` 返回 `kInvalidState`。
- 无内部线程、无回调、无事件循环 —— 全部由调用线程驱动。

## 超时与取消

- 超时由 `curl_multi_poll` 的 timeout + libcurl 选项共同实现（映射见 http-config-mapping.md）。
- 同步 API 下无"取消"入口（调用方在阻塞期间无法中断；若上层需要取消，用线程池 + 丢弃结果/关闭 client 处理）。

## 错误映射

- `curl_multi_poll`/`curl_multi_perform` 返回 `CURLMcode`；easy 完成时 `CURLcode` → `ErrorCode`（core-error.md 映射表）。
- 所有失败以 `Result<Error>` 返回，不抛异常（contracts 不变量 #3）。

## 边界与约束

- 单进程单 `HttpClient` 对应一个 `SyncEngine`（一个 CURLM）；跨 client 不共享连接池。
- 不做 c-ares（DNS 由 libcurl 内置解析器）。
- 锁串行化是 v1 简化；若并发吞吐成为瓶颈，polish 阶段可升级为"per-host 锁 + 多个 CURLM"或事件驱动。

## 评审要点

1. 锁串行化下 SC-004（100 并发）的吞吐是否达标（需基准测试验证）？
2. `curl_multi_poll` 在锁内驱动所有 easy 是否确保连接池在并发下有效复用？
3. 持锁阻塞期间 `Close()` 从另一线程调用是否安全（互斥锁语义）？
4. `total_timeout` 与 `curl_multi_poll` 超时的衔接是否正确？
