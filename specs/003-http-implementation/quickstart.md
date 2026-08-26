# Quickstart: 设计实现并验证 HTTP

**Branch**: `003-http-implementation` | **Date**: 2026-08-26 | **Spec**: [spec.md](spec.md)

## Overview

netlib 库的 HTTP 同步实现。类命名简化：`Client`/`Request`/`Response`/`Options`/`Tls`。发送 GET/POST、HTTPS（OpenSSL + 证书配置）、指定网卡。

## Usage

### 1. 创建 Client（≤10 行完成 GET）

```cpp
#include "netlib/netlib.h"

netlib::Options opts;
opts.SetConnectTimeout(std::chrono::seconds(5))
    .SetFollowRedirects(true);

netlib::Result<netlib::Client> client = netlib::Client::Create(opts);
if (!client.ok()) { /* options 校验失败 */ }

auto res = client->Get("https://httpbin.org/get");
if (!res.ok()) {
  fprintf(stderr, "Error: %s\n", res.error().message().c_str());
  return;
}
printf("Status: %d, Body: %s\n", res->status(), res->body().c_str());
```

### 2. POST + JSON body

```cpp
netlib::Request req = netlib::Request::Builder()
    .Method(netlib::Method::kPost)
    .Url("https://httpbin.org/post")
    .JsonBody(R"({"name":"netlib"})")
    .Build().value();

auto res = client->Send(req);
```

### 3. HTTPS + 证书配置

```cpp
netlib::Tls tls;
tls.SetCaFile("/path/to/custom_ca.pem");      // 内网 CA
// 或注入自签证书：
tls.SetCaCertificate("-----BEGIN CERTIFICATE-----\n...");
// mTLS 客户端证书：
tls.SetClientCertificate("/path/to/cert.pem", "/path/to/key.pem");
// 跳过校验（仅测试）：
tls.SetVerifyMode(netlib::VerifyMode::kSkipVerification);

opts.SetTls(tls);
```

### 4. 指定网卡 / 源地址

```cpp
opts.SetInterface("eth1");        // 网卡名
// 或 opts.SetLocalAddress("192.168.1.100");  // 源 IP
opts.SetLocalPort(54321);         // 源端口（可选）
```

## 测试验证

```bash
./tools/platform_setup.sh
bazel build //...
bazel test //...   # smoke + HTTP 集成 + HTTPS + 配置
```

## Next Steps

- 流式大 body → `Response::stream()`。
- 详见 [public-api.md](contracts/public-api.md) 契约。
