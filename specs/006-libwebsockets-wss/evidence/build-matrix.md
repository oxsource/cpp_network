# 构建矩阵证据（specs/006）

| 配置 | 产物 | 判定 | 时间戳 |
|------|------|------|--------|
| macos_arm64 | libwebsockets.a (ar, Mach-O arm64) + 91 公共头 | **runtime-verified path**（US1 后全链实测）| 2026-08-27 |
| android_arm64 | twin 包 cquery 分析通过；编译级待 T020 | build-pending | 2026-08-27 |
| linux_x86_64/aarch64, macos_x86_64 | 结构性路由就绪（select 同源）；executor pending | analysis-only | 2026-08-27 |

## 工程注记（Phase 2 实施沉淀）

1. **lws 源树符号链接陷阱**：仓库根存在自引用 symlink
   `minimal-examples/embedded/lhp/esp32-heltec-128-64/libwebsockets → repo root`
   —— `glob(["**"])` 触发 infinite symlink expansion。注入 BUILD 改为按子树精确圈定
   （CMakeLists/cmake/include/lib）。上游无害冗余，仅影响 Bazel 注入视图。
2. **Bazel make 展开纪律**：cmd 中 `$(location X)` 直展、bash 子壳用 `$$(dirname $(location X))`、
   环境变量引用一律 `$$(VAR)`。混用层级是本轮主要返工点。
3. **只读输出重写 EPERM**：bazel outs 为 0444；genrule 归一化前置
   `rm -rf "$(RULEDIR)/..."` + 直接 `--install-prefix $(RULEDIR)` 落盘，
   规避中间拷贝（中间 staging 的 cp 在重复运行时也会命中只读目标）。
4. **环境级 phantom-up-to-date 怪癖**（延续 005 期间观察）：本机 Bazel 6.5/bazelisk1.29
   偶发对含新 genrule 的 cc_library 报 up-to-date 但输出未物化。规避法：
   显式构建 genrule 目标一次（动作强制执行+产物落位）后再构建上层目标。
   疑似 skyframe 增量缓存误判，升级 bazelisk/bazel 版本前按上述顺序执行。

## Phase 3（US1）实施沉淀（2026-08-27）

1. **--dynamic_mode=off 固化**（根因级发现）：macOS 上 Bazel 将
   cc_library 依赖编为 `-undefined dynamic_lookup` 的 dylib；bundle 内三方
   静态对象无法在加载期解析 → 空指针调用崩溃。入 .bazelrc 全配置生效，
   与 005 的静态/符号策略天然一致。
2. **Android CMake 交叉三件套**：`CMAKE_SYSTEM_NAME=Android` +
   `SYSTEM_VERSION=API`(消掉宿主注入的 -arch) + `FIND_ROOT_PATH={openssl}`
   且 MODE_* =BOTH(FindOpenSSL 才能看见 staged 前缀)。产出 arm64 归档。
3. **就绪探针语义修正**：`sys.exit(0 if connect_ex==0 else 1)`——原式在
   连接成功时反回非零，全链路"未就绪"假象的源头之一。
4. gtest 场景映射说明：W1"受信锚成功"由 W4（注入 fixture CA 后 established）
   承载（hermetic 主机无公共根）；默认系统信任成功路径由真机 external 模式
   （T017/T020）取证。W3 默认拒自签返回 kCertificateVerificationFailed，
   lws 文案经分类器映射（x509_v_err/didn't look good 词面）。

## 全量回归（Phase 3 闸门）

bazel test //... → **7/7 PASSED**（smoke/url/headers/config/http_integration/
https/websocket），断言零删改；android 配置 //src/websocket:websocket 编译通过
（arm64 归档 737 个导出 lws_ 符号）。

## Phase 4（US2）实施沉淀（2026-08-27）

1. **lws 写通道纪律（关键根因）**：对同一条 NO_FIN 消息做"应用层 32K 窗口
   + CONTINUATION 逐窗提交"会在 lws 内部帧管线中错位——线缆实测出现
   fin=True 的 CONT(31B)/op=11 幽灵帧直至对端解析崩溃。改为**每次 writable
   提交全部剩余字节**（单次 lws_write，8MB 实测直通、内部自动分帧正确）；
   短写(partial)时推进游标、等下一次 writable 再续。教训：不要围绕
   lws 的帧状态手工再分片。
2. fixture 端修复：>125B 帧长度扩展时误置 MASK 位（0x80|126→126），该错误使
   W7 起所有大回声被判"Server must not mask"；另为主循环加 SSLError/OSError
   兜底，探针预连接不再杀死 wss 服务进程。
3. 就绪探针语义修正延续 Phase3；本阶段新增 pkill 清理孤儿 fixture 的操作惯例
   （Sandbox local 策略下 TearDown 无法覆盖上一轮泄漏）。

## 全量回归（Phase 4 闸门）

bazel test //... → **7/7 PASSED**（W6 文本+零长、W7 二进制类型保真、W8 8MB 分段透明、W9 探活不入流）。
android --config=android_arm64 //src/websocket:websocket 编译通过。

## Phase 5（US3）实施沉淀（2026-08-27）

1. **lws_close_reason 契约**：它仅"注释"关闭原因，真正发起两阶段 close 必须
   让回调返回非零。此前只设 reason 返回 0 → 永不发帧、pump 死等到超时
   （100% 稳定复现）。修正后主动关闭 3/3 稳定。
2. Close() 语义落地 FR-007/FR-009：kClosing→限时等对端确认→强制 kClosed；
   重复 Close 幂等 Ok；Close 后 Send/Receive 快速失败（kInvalidState 或带
   对端详情的 kConnectionClosed）。
3. 被动向详情透传验证：LWS_CALLBACK_WS_PEER_INITIATED_CLOSE 抓取码+原因，
   经 Error 附加载荷 (close_code/close_reason) 经 Receive 返回
   （W11: code=8, ccode=1000, reason='bye' 精确断言）。

## 全量回归（Phase 5 闸门）

bazel test //... → **7/7 PASSED**；android --config=android_arm64 编译通过。
