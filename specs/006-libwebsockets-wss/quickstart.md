# Quickstart: specs/006-libwebsockets-wss

## 一次性前提

```bash
brew install cmake            # lws 构建必需（Linux: apt install cmake）
tools/platform_setup.sh       # 检查项将包含 cmake 版本下限
```

## 最小可用（宿主 macOS/Linux）

```bash
# 全量回归（含新增 websocket_test 套件）
bazel test //... --cache_test_results=no

# 手动跑本地 ws/wss 回声演示
python3 src/tests/test_ws_server.py --port 18081 &          # 明文
python3 src/tests/test_ws_server.py --port 18443 --tls \
    --certs-dir src/tests/certs &                           # 自签 wss
./bazel-bin/src/examples/ws_demo/ws_demo                    # W1–W4 快览
```

期望：文本/二进制回声 OK；自签默认拒、注入 CA 后通过。

## 真机一键验证

```bash
make android_verify DEVICE=<serial>   # 输出含 [W1..W8] 追加段；退出码 0
```

前置：设备已连接授权；代理导出 `https_proxy=http://127.0.0.1:7890` 等。

## 依赖升级演练（SC-004 预算 ≤30min）

```bash
# 单一事实源改版本号后：
touch third_party/scripts/build-lws.sh && bazel clean && make verify && make android_build DEVICE=<serial>
```

实测基线（005 同法）：清缓存全量 ≈2min/平台段，预算余量充足。`deps_audit` 扩展断言：零 `-lwebsockets` 直链绕道。
