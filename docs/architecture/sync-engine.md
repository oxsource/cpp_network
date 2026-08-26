# 同步传输引擎（已实现）

**Branch**: `003-http-implementation` | **Date**: 2026-08-26

**对应需求**: FR-002（同步阻塞 API）、SC-001/SC-004（连接复用、毫秒级本地请求）

**实现位置**: `src/http/engine.h` / `src/http/engine.cc`

## Overview

`cpp_network::http::Engine` 是 HTTP 协议的内部传输引擎：一个由互斥锁保护的共享 `CURLM*`，`Send` 阻塞调用线程直到单次传输完成。连接池完全委托 libcurl 连接缓存。

> 相比 001 设计稿的落地差异：类名 `SyncEngine` → `Engine`；位于 `cpp_network::http`（非 `internal` 子命名空间）；配置字段 `options_`（类型 `Options`）；poll tick 固定 1000ms。

## 结构

```cpp
class Engine {                     // src/http/engine.h，内部类型
 public:
  explicit Engine(const Options& options);   // curl_multi_init + ApplyMultiOptions
  ~Engine();                                 // → Close()
  Result<Response> Send(const Request& req); // 加锁 → PerformSingle
  void Close();                              // curl_multi_cleanup，closed_ = true
 private:
  std::mutex mu_;
  CURLM* multi_;
  Options options_;                          // 持有拷贝（blob PEM 生命周期的锚点）
  bool closed_;
};
```

- `Client`（src/http/client.cc）持有 `unique_ptr<Engine>`；Client 可移动、不可拷贝。
- 并发安全：多线程 `Send` 经同一把锁串行化。
- `Close()` 后 `Send` 返回 `Error(kInvalidState)`。

## Send 流程（PerformSingle）

```text
1. curl_easy_init
2. 挂接 WriteCallback/HeaderCallback（body 与 headers 缓冲在栈上缓冲区）
3. detail::ApplyEasyOptions(easy, req, options_, &header_list)   // 全部映射，见 http-config-mapping.md
4. curl_multi_add_handle
5. 循环：curl_multi_poll(1000ms) → curl_multi_perform，直到 running == 0 或 CURLM 错误
   - 记录 started_at（steady_clock），用于超时错误码细分
6. curl_multi_info_read 找到本 easy 的 DONE 消息 → 取 result code
7. curl_multi_remove_handle → 失败路径 slist_free_all + easy_cleanup 后返回 MapCurlError
8. 成功：getinfo 取 status/effective_url
   - status == 0 → Error(kProtocolError, "missing HTTP status line")
9. 构造 Response（reason phrase 恒为 ""，流式 impl 恒 nullptr）→ 清理 → Ok
```

失败路径说明：multi 层错误（`curl_multi_perform`/`poll` 非 OK）会跳出循环，此时若无 DONE 消息，返回 `kInternalError("transfer interrupted")` 或映射后的错误。

## 超时判定与细分

libcurl 对 connect/read/hard 超时统一报 `CURLE_OPERATION_TIMEDOUT`。引擎以耗时比较细分（详见 core-error.md）：

| 触发上限 | 判定条件（elapsed ≈ 到期） | ErrorCode |
|----------|---------------------------|-----------|
| 请求级 timeout / total_timeout / write_timeout 兜底 | elapsed + 100ms ≥ 硬超时 | `kTotalTimeout` / `kWriteTimeout` |
| read_timeout 低速率检测 | elapsed + 100ms ≥ LOW_SPEED_TIME | `kReadTimeout` |
| 其余（连接阶段） | — | `kConnectionTimeout` |

## 连接复用

- 共享 `CURLM*` 的连接缓存跨请求复用 TCP/TLS 连接。
- `ApplyMultiOptions` 设置 `CURLMOPT_MAX_HOST_CONNECTIONS = max_connections_per_host()`（默认 5）。
- 无显式 `CURLMOPT_MAXCONNECTS`（libcurl 默认按需扩展缓存）。

## 边界与约束

- 单次传输内全量缓冲响应 body（流式读取 deferred，见 http-response.md）。
- 引擎与 HTTP Request/Response 强耦合；协议中立抽象（TransferOptions）为 v2 方向，见 protocol-extension.md。
