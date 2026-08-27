# Contract: 导出符号契约与审计方法（specs/005）

## 符号可见性三层组合

| 层 | 手段 | 覆盖 |
|----|------|------|
| L1 对象层 | 三方归档统一以 `-fvisibility=hidden -fPIC` 编译 | OpenSSL/curl 全部实现符号 |
| L2 库导出宏 | 既有 `CPP_NETWORK_HTTP_*` API 宏机制不变 | 本库公开 API |
| L3 链接兜底 | Linux: `-Wl,--exclude-libs,ALL`；macOS：无额外标志 | 归档内部符号残留 |

## 审计契约

- 命令约定：`nm -gU <artifact>`（macOS）/ `nm --defined-only -g <artifact>`（Linux）
- 通过条件：结果中不得出现前缀 `SSL_`、`EVP_`、`Curl_`、`curl_` 的实现符号（`curl_easy_*` 等属三方实现集，同禁）
- 抽样产物：共享形态库全量审计 + 任一静态消费二进制抽查
- 记录格式：符号审计报告存于本特性目录 `evidence/`（Phase 执行时生成），矩阵引用之

## 与既有产物的关系

- 静态消费路径行为完全不变（隐藏仅影响动态导出）
- `cpp_network_shared` 在此约束下的首次实际交付连同验证一并落在本特性
