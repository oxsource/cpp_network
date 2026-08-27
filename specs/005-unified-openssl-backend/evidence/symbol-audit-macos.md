# 符号审计取证（specs/005 Phase 5 / T013-T015）

日期：2026-08-27；产物：libcpp_network_shared.dylib（--config=macos_arm64）

## 过程修复
1. 初始状态 dylib 为**空壳**（仅 dyld_stub_binder）：export.h 旧宏为空 + 归档成员无可达锚点被链接器整体死剥离。
2. 修复组合：export.h 改为逐符号 `visibility("default")` 注解（无平台分支耦合）；src/http 的 engine/client 加 `alwayslink=1` 强制保留对象。
3. 结果：dylib 包含完整实现（总导出 9156，公开 API 命中 289）。

## 发现与用户决策
- 三方符号外泄计数 **1986**（SSL_/EVP_/Curl_/curl_）。
- 根因探明（已排除环境变量因素，env -i 复现）：Apple clang 17 / macOS 26 工具链在 .o 层不再表达 `visibility("hidden")`（连显式 attribute 都呈现为全局 T）——符号可见性控制必须移至链接期导出白名单或引入 C ABI facade，两条路均超出"TLS 后端统一"的本层范围。
- **用户决策（2026-08-27）**：本层不管理第三方符号；第三方实现符号的对外隔离延后到未来 C API 门面设计时统一处理。
