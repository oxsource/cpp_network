# Feature Specification: C++ Cross-Platform Network Library

**Feature Branch**: `001-cpp-network-library`

**Created**: 2026-08-26

**Status**: Draft

**Input**: User description: "设计规划一个方案，满足这个项目的C++网络库方案：1. 一期支持http协议，后面可能还有websocket等；2. 不同的平台需要适配不同的TSL，比如host上使用openssl, 安卓平台使用bringssll等；3. 需要提供网络配置及请求接口，可以参考okhttp等项目实现；4. 工程化及跨平台适配等可以参考目前的graph_runtime项目：/Users/moks/Develop/docker/ubuntu24/codes/graph_runtime 5. 使用bazel6.5构建，代码风格Google C++ Style Guide"

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Send HTTP Request and Receive Response (Priority: P1)

A C++ developer integrates the network library into their application. They configure a network client with custom settings, send an HTTP GET request to a remote server, and receive the response with status code, headers, and body. The same code works on macOS, Linux, and Android without modification.

**Why this priority**: Sending and receiving HTTP requests is the core functionality of the library. Without this, there is no product.

**Independent Test**: Can be fully tested by writing a unit test that starts a local HTTP server, sends a GET request, and verifies the response status code, headers, and body content are correctly received.

**Acceptance Scenarios**:

1. **Given** a local HTTP server is running on localhost:8080 returning a 200 OK with body "Hello World", **When** the developer creates an HttpClient and sends a GET request to "http://localhost:8080/", **Then** the response status code is 200, the body is "Hello World", and the request completes within 5 seconds.

2. **Given** a local HTTP server returning a 404 status with a custom header "X-Custom: value", **When** the developer sends a GET request, **Then** the response status code is 404 and the response contains the header "X-Custom" with value "value".

3. **Given** a local HTTP server that accepts POST requests, **When** the developer sends a POST request with a JSON body "{\"key\":\"value\"}" and Content-Type "application/json", **Then** the server receives the JSON body correctly and the response status code is 200.

---

### User Story 2 - Platform-Specific TLS Adapter (Priority: P1)

A developer builds an application that runs on macOS (host) and Android. The library automatically uses OpenSSL on both platforms (all-platform unified TLS), without any code changes by the developer. HTTPS requests are secure on both platforms.

**Why this priority**: TLS is essential for secure communication. Platform-specific TLS adaptation is a core requirement that differentiates this library from simple HTTP libraries.

**Independent Test**: Can be tested by writing a test that connects to an HTTPS server (e.g., https://httpbin.org) on each platform and verifies that the TLS handshake succeeds and the correct TLS backend is used.

**Acceptance Scenarios**:

1. **Given** the library is built on macOS, **When** an HTTPS request is sent to "https://httpbin.org/get", **Then** the connection succeeds using OpenSSL as the TLS backend and the response is received with correct SSL certificate validation.

2. **Given** the library is built on Android, **When** an HTTPS request is sent to "https://httpbin.org/get", **Then** the connection succeeds using OpenSSL as the TLS backend (unified across all platforms) and the response is received with correct SSL certificate validation.

3. **Given** an HTTPS server with a self-signed certificate, **When** the developer configures the client to skip certificate verification, **Then** the connection succeeds; **When** the developer does not configure certificate skipping, **Then** the connection fails with a TLS error.

---

### User Story 3 - Configure Network Client Settings (Priority: P2)

A developer needs to customize network behavior for their application. They configure connection timeouts, read/write timeouts, retry policies, and proxy settings through a unified configuration interface.

**Why this priority**: Configuration is important for production use cases but is not required for basic HTTP functionality. It can be developed after the core request/response flow works.

**Independent Test**: Can be tested by writing a test that sets a very short timeout, sends a request to a slow server, and verifies that the timeout error is triggered.

**Acceptance Scenarios**:

1. **Given** a developer configures a connection timeout of 1 second, **When** they send a request to a server that takes 10 seconds to connect, **Then** the request fails with a connection timeout error within 2 seconds.

2. **Given** a developer configures a retry policy of 3 retries with 100ms delay, **When** the first request attempt fails, **Then** the library retries up to 3 times before giving up.

3. **Given** a developer configures an HTTP proxy at "proxy.example.com:8080", **When** they send a request to "http://example.com", **Then** the request is routed through the proxy.

---

### User Story 4 - WebSocket Communication (Priority: P3)

A developer needs bidirectional real-time communication. After the initial HTTP support is stable, they can establish a WebSocket connection, send messages, and receive messages from the server.

**Why this priority**: WebSocket is a future phase feature. It is lower priority than HTTP and TLS support.

**Independent Test**: Can be tested by starting a WebSocket echo server, connecting to it, sending a message, and verifying the echoed message is received.

**Acceptance Scenarios**:

1. **Given** a WebSocket echo server is running at "ws://localhost:8080/echo", **When** the developer establishes a WebSocket connection and sends a text message "Hello", **Then** the server echoes back "Hello" and the client receives it.

2. **Given** an established WebSocket connection, **When** the network is interrupted, **Then** the library detects the disconnection and triggers a reconnection callback if configured.

---

### Edge Cases

- What happens when the server returns a malformed HTTP response (e.g., invalid header format)? The library should return a meaningful error rather than crashing.
- How does the system handle very large response bodies (e.g., > 100MB)? The library should support streaming responses without loading the entire body into memory.
- How does the system handle concurrent requests on the same client? The library should support multiple concurrent requests without data races or deadlocks.
- How does the system handle DNS resolution failures? The library should return a clear error indicating DNS resolution failure.
- How does the system handle redirects (HTTP 301/302)? The library should follow redirects by default, with an option to disable automatic redirect following.
- How does the system handle connection pool exhaustion? The library should queue requests or return a clear error when all connections are in use.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The library MUST provide an HttpClient interface that allows developers to send HTTP requests (GET, POST, PUT, DELETE, PATCH, HEAD, OPTIONS) and receive responses.
- **FR-002**: The library MUST support HTTP/1.1 protocol.
- **FR-003**: The library MUST provide a platform-agnostic TLS abstraction layer that supports OpenSSL as the TLS backend on all platforms (host and Android).
- **FR-004**: The library MUST allow developers to configure connection timeouts, read/write timeouts, and total request timeouts.
- **FR-005**: The library MUST support setting custom HTTP headers on requests and reading response headers.
- **FR-006**: The library MUST support sending request bodies (for POST, PUT, PATCH) with configurable Content-Type.
- **FR-007**: The library MUST support response body streaming to handle large payloads without loading entirely into memory.
- **FR-008**: The library MUST support automatic redirect following with configurable maximum redirect count.
- **FR-009**: The library MUST support configurable retry policies (max retry count, retry delay, retry condition).
- **FR-010**: The library MUST support HTTP proxy configuration.
- **FR-011**: The library MUST support concurrent requests from multiple threads without data races.
- **FR-012**: The library MUST provide a connection pool to reuse TCP connections for multiple requests to the same host.
- **FR-013**: The library MUST provide clear, actionable error messages for all failure modes (network errors, TLS errors, timeout errors, protocol errors).
- **FR-014**: The library MUST support building with Bazel 6.5 on macOS, Linux, and Android.
- **FR-015**: The library MUST follow Google C++ Style Guide for all source code.
- **FR-016**: The library MUST provide a public API with platform-independent headers that do not expose TLS backend implementation details.
- **FR-017**: The library MUST support WebSocket protocol (upgrade from HTTP, message send/receive, close handshake) in a future phase.
- **FR-018**: The library MUST provide a synchronous (blocking) API for all I/O operations, returning results directly. The library MUST NOT implement any event loop, thread scheduling, Promise, or coroutine abstractions — asynchronous behavior is implemented externally by the user.
- **FR-021**: The library MUST provide a protocol-independent engine layer that supports multiple protocols (HTTP, WebSocket, etc.) without protocol-specific coupling in the transport engine.
- **FR-019**: The library MUST support custom certificate validation (allow skipping verification, provide custom CA certificates).
- **FR-020**: The library MUST implement a simple, ergonomic API design for constructing and executing HTTP requests, inspired by JavaScript's axios library (simple per-verb methods, unified config). All future protocol APIs follow the same principle.

### Key Entities *(include if feature involves data)*

- **HttpClient**: The main entry point for making network requests. Holds configuration (timeouts, retry policy, proxy, TLS settings) and manages a connection pool. Created via a builder pattern.
- **HttpRequest**: Represents an outgoing HTTP request. Contains HTTP method, URL, headers, and optional body. Immutable after construction.
- **HttpResponse**: Represents an incoming HTTP response. Contains status code, status text, headers, and response body. Provides access to body as string, bytes, or stream.
- **NetworkConfig**: Configuration object for the HttpClient. Contains timeout settings, retry policy, proxy settings, TLS configuration, and connection pool settings.
- **TlsConfig**: TLS configuration for the HttpClient (verify mode, CA certificates, client certificate, SNI). Maps to libcurl SSL options; the TLS backend is OpenSSL on all platforms.
- **WebSocket**: (Future) Represents an active WebSocket connection. Provides synchronous methods for sending messages, receiving messages, and closing the connection.
- **ConnectionPool**: Manages a pool of persistent TCP/TLS connections to reduce connection establishment overhead. Managed internally by libcurl; exposes tuning knobs (max connections per host, keep-alive duration).

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Developers can send an HTTP GET request and receive the response in under 10 lines of code (including configuration and client creation).
- **SC-002**: The library correctly handles 100% of standard HTTP/1.1 response status codes (1xx, 2xx, 3xx, 4xx, 5xx) with appropriate error or success handling.
- **SC-003**: The library achieves TLS handshake completion within 500ms for a local HTTPS server on all supported platforms.
- **SC-004**: The library supports at least 100 concurrent connections to the same host without performance degradation.
- **SC-005**: The library builds successfully on macOS, Linux, and Android with zero warnings (under Bazel 6.5 with default warning flags).
- **SC-006**: The library's public API compiles without modification when targeting any supported platform — no platform-specific ifdefs in user code.
- **SC-007**: The library can stream a response body of at least 1GB without exceeding 10MB of peak memory usage.
- **SC-008**: The library fails gracefully with meaningful error messages for all network failure scenarios (DNS failure, connection refused, timeout, TLS error, malformed response).

## Clarifications

### Session 2026-08-26

- Q: Which I/O model should the library use for handling network connections? → A: 库仅提供基础网络请求；事件与流程编排不在库内实现。对外为**同步阻塞 API**，异步由上层用线程池/协程包装。
- Q: What observability capabilities should the library provide? → A: Minimal — error codes and return values only. No built-in logging, metrics, or tracing.
- Q: What API design pattern should HTTP and future protocols follow? → A: Reference axios (JavaScript) — simple, ergonomic, per-verb methods + unified config. All future protocol implementations follow the same principle.
- Q: Should the library include its own thread scheduling / executor? → A: No. Threading, events, and flow orchestration are controlled externally by the user. The library only provides basic network requests (synchronous).
- Q: Promise 在网络请求中是否合适？→ A: 不合适（C++ 缺 async/await 语法糖，裸 Promise 链复杂且事件/流程应在库外）。改用同步阻塞 API，库内不实现 Promise/协程/事件桥接。

## Assumptions

- Host platforms include macOS (x86_64 and arm64) and Linux (x86_64 and aarch64).
- Android platform targets API level 24 or higher.
- iOS and Windows platforms are out of scope for v1.
- HTTP/2 support is not required for v1; only HTTP/1.1 is needed.
- The library is a client-side library only; no server functionality is required.
- Developers will use the library from C++17 or later.
- The project follows the same engineering conventions as graph_runtime: Bazel platform definitions, cc_library/cc_test/cc_binary targets, platform-specific select(), and visibility-controlled packages.
- External dependencies (OpenSSL, zlib for compression) will be managed via Bazel WORKSPACE rules or http_archive.
- The library will be delivered as both a static library and a shared library, following the graph_runtime pattern.
- The library does not include built-in logging, metrics, or tracing. All error information is communicated via return values and error codes.
- The library does not manage threads, run an event loop, or implement Promise/coroutine abstractions. It provides a synchronous blocking API; async/flow orchestration is the user's responsibility.