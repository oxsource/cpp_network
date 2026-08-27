# ADR-004: WebSocket 传输内核选型——libwebsockets（静态、单 TLS 栈）

日期：2026-08-27 ｜ 状态：Accepted ｜ 关联：specs/006-libwebsockets-wss

## Context

001 草案曾规划以 libcurl ≥7.86 的 `curl_ws_*` API 承载 WebSocket；specs/005
将全平台 curl 钉在 8.7.1 且移除系统传输层。用户决策新增 WebSocket 能力时点名
集成 libwebsockets，并要求 ws/wss 双通道兼容（澄清记录于 spec Clarifications）。

## Decision

1. 传输内核采用 **libwebsockets v4.5.8**（stable 4.5 线头部，sha256 实测入册
   `cpp_network_deps.bzl` 单一事实源），CMake 最小特性集构建：客户端-only、
   无扩展(permessage-deflate 从二进制层排除)、纯静态、`LWS_WITHOUT_SERVER=ON`。
2. **单一 TLS 栈纪律延续（ADR-003 v3）**：lws 经 `OPENSSL_ROOT_DIR` 直指 TLS
   bundle 安装树（`//third_party/tls/{host,android}` 新增 `bundle_tar` 单标签
   交接产物），信任锚/客户端证书全部走 lws 内存接口——无临时文件、无第二份
   OpenSSL。
3. 同步阻塞 API 包装每连接独立 lws_context；调用线程即 pump 线程（research D4），
   与既有 Result/Options/Tls 语义无缝拼装。

## Alternatives considered

| 方案 | 否决理由 |
|------|----------|
| curl_ws_* | 依赖 curl≥7.86 与我们锁定版本冲突面更大；多路复用引擎耦合见 research D4 |
| uWebSockets / websocketpp 等 | 非 C ABI 或重模板框架，违背库定位与符号策略 |

## Consequences

- 正面：跨平台行为一致、依赖治理闭环（deps_audit 扩展断言零 `-lwebsockets`
  绕道）、升级演练流程复用 ≤30min 预算实测通过
- 成本：引入 CMake 为第三方构建前提（platform_setup.sh 检查项）；lws 源树含
  自引用 symlink 需注入 BUILD 子树圈定（evidence 注记）；Bazel dylib 动态模式
  对三方静态对象不兼容 → 全局 `--dynamic_mode=off`（Phase3 根因）
- 后续锚点：permessage-deflate、自动重连仍为显式排除项；二进制体积增量与
  符号隔离策略沿用 005 契约
