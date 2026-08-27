# Quickstart: 全平台统一 OpenSSL TLS 后端

**Branch**: `005-unified-openssl-backend`

## 对使用者的变化

- **零 API 变化**：应用代码跨平台一字不改
- 宿主构建不再要求系统安装 libcurl/openssl 开发包；克隆后按既有命令直接构建
- 共享形态产物仅导出公开 API 符号

## 标准流程（与 004 相同的入口）

```bash
# macOS（运行级验收平台）
make verify            # 全量 gtest 回归
bazel build //src/examples/http_demo:http_demo && \
  ./bazel-bin/src/examples/http_demo/http_demo   # 外网 HTTPS 冒烟

# Android（已验收，回归保护）
make android_verify DEVICE=<serial>

# Linux x86_64 / aarch64（构建级验证；拿到目标机后可补运行证据）
bazel build --config=linux_x86_64 //src/public:cpp_network
```

## 版本升级演练（FR-004 / SC-002，30 分钟内完成）

1. 编辑 `cpp_network_deps.bzl`：仅改对应条目的 URL、strip_prefix 与 sha256
2. `bazel clean && make verify` + 各平台 `android_build` / 构建级验证
3. 更新 `docs/architecture/tls-config.md` 矩阵中的版本标注并提交

单一事实源约束：**禁止**在 `.bzl` 之外的地方固定版本号。

## 符号审计（共享形态交付时执行一次）

```bash
nm -gU bazel-bin/src/public/libcpp_network.so | grep -E "SSL_|EVP_|Curl_|curl_"
# 预期输出为空；非空即违反 contracts/symbol-visibility.md
```

## 故障排查

| 现象 | 处理 |
|------|------|
| 链接报归档格式错误 | 确认未覆盖 PATH 导致 GNU ar 抢占；脚本已显式选系统归档器 |
| Linux 上报缺 cc/make | 安装基础编译组（build-essential 等）——首次即被前置检查提示 |
| 证书校验失败但内容正确 | 核对注入锚来源；统一后所有平台行为一致，可对照另一平台同场景结果定位 |
