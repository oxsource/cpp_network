// WebSocket connection scenarios (specs/006 US1, research.md D7 W1-W5).
//
// Hermetic-strategy note: the host gtest cannot rely on public CA-signed
// endpoints, so "trusted anchor -> connect success" is exercised with the
// fixture CA injected in memory (W4) right after asserting the same
// self-signed endpoint is REJECTED by default (W3). The default system
// trust-store success path is proven on-device by the external-mode E/W
// scenarios (contracts/device-scenarios.md), mirroring how https_test
// layered its host/device coverage.
#include "http/http_umbrella.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>

#include "test_util.h"
#include "gtest/gtest.h"

namespace cpp_network {
namespace ws {

using cpp_network::comm::ErrorCode;
using cpp_network::comm::Options;
using cpp_network::comm::Tls;
using cpp_network::http::CertPath;
using cpp_network::http::TestAssetRoot;

namespace {

constexpr int kWsPort = 18086;   // plaintext fixture
constexpr int kWssPort = 18446;  // self-signed wss fixture
constexpr int kPingPort = 18087; // plaintext fixture with unsolicited pings
constexpr int kPeerClosePort = 18088; // fixture closing right after handshake
pid_t g_ws_pid = -1;
pid_t g_wss_pid = -1;
pid_t g_ping_pid = -1;
pid_t g_peer_close_pid = -1;

Options BaseOptions() {
  Options opts;
  opts.SetConnectTimeout(std::chrono::seconds(5))
      .SetReadTimeout(std::chrono::seconds(5));
  return opts;
}

bool ReadWholeFile(const std::string& path, std::string* out) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) return false;
  std::ostringstream buffer;
  buffer << file.rdbuf();
  *out = buffer.str();
  return true;
}

std::string FixtureCertsDir() { return TestAssetRoot() + "/certs"; }

pid_t StartServer(const std::vector<std::string>& extra) {
  pid_t pid = fork();
  if (pid == 0) {
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>("python3"));
    argv.push_back(const_cast<char*>("src/tests/test_ws_server.py"));
    for (const auto& a : extra) {
      argv.push_back(const_cast<char*>(a.c_str()));
    }
    argv.push_back(nullptr);
    execvp(argv[0], argv.data());
    _exit(127);
  }
  return pid;
}

bool WaitReady(int port, int attempts = 60) {
  const std::string cmd =
      "python3 -c \"import socket,sys;s=socket.socket();s.settimeout(0.4);"
      "sys.exit(0 if s.connect_ex(('127.0.0.1'," +
      std::to_string(port) + "))==0 else 1)\" >/dev/null 2>&1";
  for (int i = 0; i < attempts; ++i) {
    if (system(cmd.c_str()) == 0) return true;
    usleep(100000);
  }
  return false;
}

class WebSocketTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    signal(SIGPIPE, SIG_IGN);
    g_ws_pid = StartServer({"--port", std::to_string(kWsPort)});
    ASSERT_NE(g_ws_pid, -1);
    ASSERT_TRUE(WaitReady(kWsPort)) << "plaintext ws fixture not ready";

    g_wss_pid = StartServer({"--port", std::to_string(kWssPort), "--tls",
                             "--certs-dir", FixtureCertsDir()});
    ASSERT_NE(g_wss_pid, -1);
    ASSERT_TRUE(WaitReady(kWssPort)) << "wss fixture not ready";
    g_ping_pid = StartServer({"--port", std::to_string(kPingPort),
                              "--inject-ping"});
    ASSERT_NE(g_ping_pid, -1);
    ASSERT_TRUE(WaitReady(kPingPort)) << "ping fixture not ready";
    g_peer_close_pid =
        StartServer({"--port", std::to_string(kPeerClosePort), "--peer-close",
                     "1000", "--reason", "bye"});
    ASSERT_NE(g_peer_close_pid, -1);
    ASSERT_TRUE(WaitReady(kPeerClosePort))
        << "peer-close fixture not ready";
  }

  static void TearDownTestSuite() {
    for (pid_t pid : {g_ws_pid, g_wss_pid, g_ping_pid,
                        g_peer_close_pid}) {
      if (pid > 0) kill(pid, SIGKILL);
    }
    int status = 0;
    while (waitpid(-1, &status, WNOHANG) > 0) {
    }
  }
};

TEST_F(WebSocketTest, W2PlaintextConnectOpensSession) {
  auto r = WebSocket::Connect(
      "ws://127.0.0.1:" + std::to_string(kWsPort) + "/echo", BaseOptions());
  ASSERT_TRUE(r.ok()) << r.error().message();
  EXPECT_TRUE(r.value().IsOpen());
}

TEST_F(WebSocketTest, W3SelfSignedRejectedByDefault) {
  auto r = WebSocket::Connect(
      "wss://127.0.0.1:" + std::to_string(kWssPort) + "/secure",
      BaseOptions());
  ASSERT_FALSE(r.ok());
  EXPECT_EQ(r.error().code(), ErrorCode::kCertificateVerificationFailed)
      << r.error().message();
}

TEST_F(WebSocketTest, W4InMemoryCaPemInjectionAccepted) {
  std::string pem;
  ASSERT_TRUE(ReadWholeFile(CertPath("ca_cert.pem"), &pem));
  Options opts = BaseOptions();
  Tls tls = Tls::Builder().SetCaPem(pem).Build();
  opts.SetTls(tls);
  auto r = WebSocket::Connect(
      "wss://127.0.0.1:" + std::to_string(kWssPort) + "/secure", opts);
  ASSERT_TRUE(r.ok()) << r.error().message();
  EXPECT_TRUE(r.value().IsOpen());
}

TEST_F(WebSocketTest, W5NonWsSchemeFailsFastBeforeNetworkActivity) {
  auto r = WebSocket::Connect("ftp://example.org/file", BaseOptions());
  ASSERT_FALSE(r.ok());
  EXPECT_EQ(r.error().code(), ErrorCode::kInvalidArgument);
  // Fast-fail contract: zero network activity. Empty input must yield the
  // identical argument error rather than any DNS/connection code.
  auto r2 = WebSocket::Connect("", BaseOptions());
  EXPECT_FALSE(r2.ok());
  EXPECT_EQ(r2.error().code(), ErrorCode::kInvalidArgument);
}

std::string ConnectUrl(int port) {
  return "ws://127.0.0.1:" + std::to_string(port) + "/echo";
}

TEST_F(WebSocketTest, W6TextEchoRoundTripWithZeroLengthLegality) {
  auto r = WebSocket::Connect(ConnectUrl(kWsPort), BaseOptions());
  ASSERT_TRUE(r.ok()) << r.error().message();
  WebSocket ws = r.TakeValue();

  WsMessage text;
  text.is_text = true;
  const std::string payload = "echo me: 中文 \u2764 bytes";
  text.data.assign(payload.begin(), payload.end());
  ASSERT_TRUE(ws.Send(text).ok());

  auto got = ws.Receive();
  ASSERT_TRUE(got.ok()) << got.error().message();
  EXPECT_TRUE(got.value().is_text);
  EXPECT_EQ(std::string(got.value().data.begin(), got.value().data.end()),
            payload);

  // Zero-length frames are legal (edge-case decision).
  WsMessage empty;
  empty.is_text = true;
  ASSERT_TRUE(ws.Send(empty).ok());
  auto empty_back = ws.Receive();
  ASSERT_TRUE(empty_back.ok());
  EXPECT_EQ(empty_back.value().data.size(), size_t{0});
}

TEST_F(WebSocketTest, W7BinaryEchoPreservesTypeAndBytes) {
  auto r = WebSocket::Connect(ConnectUrl(kWsPort), BaseOptions());
  ASSERT_TRUE(r.ok());
  WebSocket ws = r.TakeValue();

  WsMessage bin;
  bin.is_text = false;
  for (int i = 0; i < 256; ++i) {
    bin.data.push_back(static_cast<uint8_t>(i));
  }
  ASSERT_TRUE(ws.Send(bin).ok());

  auto got = ws.Receive();
  ASSERT_TRUE(got.ok());
  EXPECT_FALSE(got.value().is_text);
  EXPECT_EQ(got.value().data, bin.data);
}

TEST_F(WebSocketTest, W8LargeMessageFragmentationTransparent) {
  auto r = WebSocket::Connect(ConnectUrl(kWsPort), BaseOptions());
  ASSERT_TRUE(r.ok());
  WebSocket ws = r.TakeValue();

  static constexpr size_t kPayloadSize = 8 * 1024 * 1024;
  WsMessage big;
  big.is_text = false;
  big.data.resize(kPayloadSize);
  for (size_t i = 0; i < kPayloadSize; ++i) {
    big.data[i] = static_cast<uint8_t>(i * 31 + (i >> 8));
  }
  auto sent = ws.Send(big);
  ASSERT_TRUE(sent.ok()) << "big-send failed: code="
                         << static_cast<int>(sent.error().code())
                         << " msg=" << sent.error().message()
                         << " sent=" << 0;

  auto got = ws.Receive();
  ASSERT_TRUE(got.ok()) << got.error().message();
  EXPECT_EQ(got.value().data.size(), kPayloadSize);
  EXPECT_EQ(got.value().data, big.data);  // FR-005: one Receive, whole message
}

TEST_F(WebSocketTest, W9PingTransparencyDoesNotDisturbStream) {
  auto r = WebSocket::Connect(ConnectUrl(kPingPort), BaseOptions());
  ASSERT_TRUE(r.ok());
  WebSocket ws = r.TakeValue();

  WsMessage first;
  first.is_text = true;
  const std::string p1 = "before ping";
  first.data.assign(p1.begin(), p1.end());
  ASSERT_TRUE(ws.Send(first).ok());

  WsMessage second;
  second.is_text = true;
  const std::string p2 = "after ping";
  second.data.assign(p2.begin(), p2.end());
  ASSERT_TRUE(ws.Send(second).ok());

  for (const std::string* expected : {&p1, &p2}) {
    auto got = ws.Receive();
    ASSERT_TRUE(got.ok()) << got.error().message();
    EXPECT_EQ(std::string(got.value().data.begin(), got.value().data.end()),
              *expected)
        << "ping frames must never surface as messages";
  }
}

TEST_F(WebSocketTest, W10ActiveCloseHandshakeIdempotent) {
  auto r = WebSocket::Connect(ConnectUrl(kWsPort), BaseOptions());
  ASSERT_TRUE(r.ok());
  WebSocket ws = r.TakeValue();
  ASSERT_TRUE(ws.IsOpen());

  auto closed = ws.Close(WsCloseCode::kNormal, "done");
  ASSERT_TRUE(closed.ok()) << closed.error().message();
  EXPECT_FALSE(ws.IsOpen());

  // Post-close operations fail fast; the exact code depends on whether the
  // peer's acknowledgement carried details back (kConnectionClosed w/ code)
  // or we terminalized locally (kInvalidState).
  auto send_after = ws.Send(WsMessage{});
  EXPECT_FALSE(send_after.ok());
  EXPECT_TRUE(send_after.error().code() == ErrorCode::kInvalidState ||
              send_after.error().code() == ErrorCode::kConnectionClosed)
      << static_cast<int>(send_after.error().code());
  auto recv_after = ws.Receive();
  EXPECT_FALSE(recv_after.ok());

  // FR-007: repeated Close is idempotent and never raises.
  auto second = ws.Close(WsCloseCode::kGoingAway, "again");
  EXPECT_TRUE(second.ok());
}

TEST_F(WebSocketTest, W11PeerCloseCarriesDetailsThroughReceive) {
  auto r = WebSocket::Connect(
      "ws://127.0.0.1:" + std::to_string(kPeerClosePort) + "/bye",
      BaseOptions());
  ASSERT_TRUE(r.ok()) << r.error().message();

  auto got = r.value().Receive();  // peer tears down immediately
  ASSERT_FALSE(got.ok());
  EXPECT_EQ(got.error().code(), ErrorCode::kConnectionClosed);
  EXPECT_EQ(got.error().close_code(), uint16_t{1000});
  EXPECT_EQ(got.error().close_reason(), std::string("bye"));

  // And the session is dead afterwards: fast-fail, no hang.
  auto resend = r->Send(WsMessage{});
  EXPECT_FALSE(resend.ok());
}

}  // namespace
}  // namespace ws
}  // namespace cpp_network
