# Requirement Traceability Matrix

**Branch**: `001-cpp-network-library` | **Date**: 2026-08-26（同步重构版）

核对 spec.md 的 FR-001..FR-021 与 SC-001..SC-008 在设计文档中的覆盖情况。

## 功能需求 → 设计文档

| FR | 需求 | 设计文档覆盖 | 状态 |
|----|------|--------------|------|
| FR-001 | HttpClient 接口（GET/POST/.../Send） | [http-client-api.md](../architecture/http-client-api.md) | ✅ |
| FR-002 | HTTP/1.1 | [http-transfer-lifecycle.md](../architecture/http-transfer-lifecycle.md)、libcurl 引擎 | ✅ |
| FR-003 | 平台无关 TLS 抽象（全平台 OpenSSL） | [tls-backend-selection.md](../architecture/tls-backend-selection.md) | ✅ |
| FR-004 | timeout 配置（连接/读写/总） | [http-config-mapping.md](../architecture/http-config-mapping.md) | ✅ |
| FR-005 | 自定义 headers | [http-request.md](../architecture/http-request.md) | ✅ |
| FR-006 | 请求体 + Content-Type | [http-request.md](../architecture/http-request.md) | ✅ |
| FR-007 | 响应流式 | [http-response.md](../architecture/http-response.md) | ✅ |
| FR-008 | 重定向（max_redirects） | [http-config-mapping.md](../architecture/http-config-mapping.md) | ✅ |
| FR-009 | 可配置重试策略 | [retry-policy.md](../architecture/retry-policy.md)（上层实现） | ✅ |
| FR-010 | HTTP 代理 | [proxy-config.md](../architecture/proxy-config.md) | ✅ |
| FR-011 | 多线程并发安全 | [sync-engine.md](../architecture/sync-engine.md) | ✅ |
| FR-012 | 连接池复用 | [connection-pool.md](../architecture/connection-pool.md) | ✅ |
| FR-013 | 清晰错误信息 | [core-error.md](../architecture/core-error.md) | ✅ |
| FR-014 | Bazel 6.5 macOS/Linux/Android | [bazel-platforms.md](../architecture/bazel-platforms.md) | ✅ |
| FR-015 | Google C++ Style | 设计规范（README.md）、实现阶段约束 | ✅ |
| FR-016 | 平台无关公共 API | [tls-backend-selection.md](../architecture/tls-backend-selection.md)、contracts | ✅ |
| FR-017 | WebSocket（未来） | [websocket-api.md](../architecture/websocket-api.md) | ✅ |
| FR-018 | 同步 API（修订） | [sync-engine.md](../architecture/sync-engine.md)、[contracts](../contracts/public-api.md) | ✅ |
| FR-019 | 自定义证书校验 | [tls-config.md](../architecture/tls-config.md)、[tls-cert-validation.md](../architecture/tls-cert-validation.md) | ✅ |
| FR-020 | axios 风格 API | [http-client-api.md](../architecture/http-client-api.md) | ✅ |
| FR-021 | 协议无关引擎层 | [protocol-extension.md](../architecture/protocol-extension.md) | ✅ |

## 成功标准 → 验证路径

| SC | 标准 | 设计支持 | 验证方式（实现阶段） |
|----|------|----------|----------------------|
| SC-001 | GET 请求 <10 行代码 | quickstart 示例 | 示例编译运行 |
| SC-002 | 100% HTTP 状态码处理 | http-response.md 状态码处理 | 本地服务器集成测试 |
| SC-003 | TLS 握手 <500ms | TLS 设计 | 基准测试 |
| SC-004 | 100 并发连接 | sync-engine/connection-pool | 并发压力测试 |
| SC-005 | 零警告构建 | bazel-platforms 验证矩阵 | `bazel build --config=<platform>` |
| SC-006 | 平台无关 API 编译一致 | contracts 不变量 #1 | 三平台编译同一示例 |
| SC-007 | 1GB 流式 <10MB 内存 | http-response 流式模式（64KB 块） | 大文件集成测试 |
| SC-008 | 优雅失败所有场景 | core-error 映射表 | 故障注入集成测试 |

## 用户故事 → 设计文档

| US | 故事 | 覆盖文档 |
|----|------|----------|
| US1 (P1) | HTTP 请求/响应 | http-client-api / http-request / http-response / http-transfer-lifecycle |
| US2 (P1) | 平台 TLS（全平台 OpenSSL） | tls-backend-selection / tls-config / tls-cert-validation / host-openssl-build |
| US3 (P2) | 网络配置 | network-config / retry-policy / proxy-config / connection-pool / http-config-mapping |
| US4 (P3) | WebSocket（未来） | websocket-api / websocket-message-flow / protocol-extension |

## 覆盖缺口

- **FR-015（Google 风格）**：当前仅为约束声明，实现阶段的 lint/format 配置任务未在本设计文档覆盖（属实现阶段工具配置，不算架构缺口）。
- **SC-005/SC-008 的自动化验证**：需实现阶段建立 CI 集成测试，当前架构层仅定义了验证路径。

## 评审要点

1. 每个 FR 是否至少有一个设计文档支撑？
2. SC 是否有明确验证方式（可在实现阶段执行）？
3. 是否有需求在文档间相互矛盾（如异步残留）？
