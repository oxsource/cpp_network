// Device-side end-to-end checks for the cpp_network HTTP client (specs/004
// contracts/device-test-contract.md).
//
// One self-contained binary that exercises the public API exactly like an
// application would: seven scenarios (S1-S7 per data-model.md Entity 4) run
// against local test services reachable via 127.0.0.1 (host fixtures behind
// `adb reverse` on Android, direct sockets on the host). Every scenario uses
// an independent Client; failures are collected, not fatal.
//
// Exit codes: 0 = all passed; otherwise first-failing-scenario-id + 1.
#include "http/http_umbrella.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "test_util.h"

using cpp_network::http::CertPath;
using cpp_network::http::Client;
using cpp_network::http::ErrorCode;
using cpp_network::http::Error;
using cpp_network::http::ExtCaBundle;
using cpp_network::http::HttpBase;
using cpp_network::http::MtlsBase;
using cpp_network::http::Options;
using cpp_network::http::Request;
using cpp_network::http::Result;
using cpp_network::http::Response;
using cpp_network::http::Tls;
using cpp_network::http::TlsBase;
using cpp_network::http::WsMtlsWsBase;
using cpp_network::http::WsPeerCloseBase;
using cpp_network::http::WsPlainBase;
using cpp_network::http::WsTlsBase;
using cpp_network::http::ExtWsBase;
using cpp_network::http::WebSocket;
using cpp_network::http::WsCloseCode;
using cpp_network::http::WsMessage;

namespace {

std::string ReadFileOrDie(const std::string& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream.is_open()) {
    std::fprintf(stderr, "[device-e2e] cannot open asset: %s\n", path.c_str());
    std::exit(98);  // asset packaging problem, not a scenario failure
  }
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  return buffer.str();
}

Options ShortTimeouts() {
  Options options;
  options.SetConnectTimeout(std::chrono::milliseconds(5000));
  options.SetReadTimeout(std::chrono::milliseconds(5000));
  options.SetTotalTimeout(std::chrono::milliseconds(15000));
  return options;
}

struct Scenario {
  int id;
  const char* name;
  bool (*run)(std::string* detail);
};

// Default (external) mode: real-world HTTPS against public endpoints — the
// user-approved validation scope for Android devices whose network differs
// from the development host (no shared segment, no adb reverse needed).
//
// Trust anchor: VerifiedWithExtAnchor() injects the platform store bundle
// (see test_util.h ExtCaBundle) so verified-peer requests behave like a
// well-behaved application following the FR-003/ADR-003 injection pattern.
std::string ExtSiteBase() {
  const char* v = std::getenv("NETLIB_TEST_EXT_BASE");
  return (v != nullptr && *v != '\0') ? std::string(v)
                                      : std::string("https://example.com");
}

Options VerifiedWithExtAnchor() {
  Options options = ShortTimeouts();
  std::string bundle = ExtCaBundle();
  if (!bundle.empty()) {
    options.SetTls(Tls::Builder().SetCaFile(bundle).Build());
  }
  return options;
}

bool E1_SiteGet(std::string* detail) {
  auto client = Client::Create(VerifiedWithExtAnchor());
  if (!client.ok()) {
    *detail = client.error().message();
    return false;
  }
  Result<Response> res = client->Get(ExtSiteBase() + "/");
  if (!res.ok()) {
    *detail = "[" +
              std::string(cpp_network::http::ErrorCodeToString(
                  res.error().code())) +
              "] " + res.error().message();
    return false;
  }
  if (res->status() != 200 || res->body().find("Example Domain") ==
                                  std::string::npos) {
    *detail = "status " + std::to_string(res->status());
    return false;
  }
  return true;
}

bool E2_SiteHeaderRead(std::string* detail) {
  auto client = Client::Create(VerifiedWithExtAnchor());
  if (!client.ok()) {
    *detail = client.error().message();
    return false;
  }
  Result<Response> res = client->Head(ExtSiteBase() + "/");
  if (!res.ok() || res->status() != 200) {
    *detail = res.ok() ? "status " + std::to_string(res->status())
                       : "[" +
                             std::string(cpp_network::http::ErrorCodeToString(
                                 res.error().code())) +
                             "] " + res.error().message();
    return false;
  }
  auto ct = res->GetHeader("content-TYPE");  // case-insensitive lookup
  if (!ct.has_value() || ct->find("text/html") == std::string::npos) {
    *detail = "missing text/html Content-Type";
    return false;
  }
  return true;
}

bool E3_PostJsonEcho(std::string* detail) {
  auto client = Client::Create(VerifiedWithExtAnchor());
  if (!client.ok()) {
    *detail = client.error().message();
    return false;
  }
  Result<Request> req =
      Request::Builder()
          .SetMethod(cpp_network::http::Method::kPost)
          .Url("https://httpbin.org/post")
          .JsonBody("{\"hello\":\"android\"}")
          .Timeout(std::chrono::milliseconds(15000))
          .Build();
  if (!req.ok()) {
    *detail = req.error().message();
    return false;
  }
  Result<Response> res = client->Send(req.value());
  if (!res.ok()) {
    *detail = "[" +
              std::string(cpp_network::http::ErrorCodeToString(
                  res.error().code())) +
              "] " + res.error().message();
    return false;
  }
  if (res->status() != 200 ||
      res->body().find("hello") == std::string::npos ||
      res->body().find("android") == std::string::npos) {
    *detail = "status " + std::to_string(res->status()) + " no echo";
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// WebSocket scenarios (specs/006, contracts/device-scenarios.md).
// External list runs against a public trusted wss echo; local list exercises
// the host fixtures (plain/tls/mTLS-ws/peer-close) through adb reverse.
// ---------------------------------------------------------------------------

bool W1_WssPublicEcho(std::string* detail) {
  // Explicit ext-store anchor via SetCaFile: routes into lws' filepath
  // client-CA form, whose OpenSSL load path digests the multi-cert merged
  // store (the memory form decodes only a single PEM block).
  auto r = WebSocket::Connect(ExtWsBase(), VerifiedWithExtAnchor());
  if (!r.ok()) {
    *detail = r.error().message();
    return false;
  }
  WebSocket ws = r.TakeValue();
  if (!ws.IsOpen()) {
    *detail = "not open after handshake";
    return false;
  }
  WsMessage out;
  out.is_text = true;
  const std::string payload = "device-wss-echo";
  out.data.assign(payload.begin(), payload.end());
  if (!ws.Send(out).ok()) {
    *detail = "send failed";
    return false;
  }
  auto got = ws.Receive();
  if (!got.ok()) {
    *detail = got.error().message();
    return false;
  }
  if (!got.value().is_text ||
      std::string(got.value().data.begin(), got.value().data.end()) !=
          payload) {
    *detail = "echo mismatch";
    return false;
  }
  if (!ws.Close(WsCloseCode::kNormal, "done").ok()) {
    *detail = "close handshake failed";
    return false;
  }
  return true;
}

bool W2_PlainConnectOpens(std::string* detail) {
  auto r = WebSocket::Connect(WsPlainBase(), ShortTimeouts());
  if (!r.ok()) {
    *detail = r.error().message();
    return false;
  }
  if (!r.value().IsOpen()) {
    *detail = "session not open";
    return false;
  }
  return true;
}

bool W3_SelfSignedWssRejectedByDefault(std::string* detail) {
  auto r = WebSocket::Connect(WsTlsBase(), ShortTimeouts());
  if (r.ok()) {
    *detail = "self-signed accepted by default";
    return false;
  }
  if (r.error().code() != ErrorCode::kCertificateVerificationFailed) {
    *detail =
        "[" + std::string(cpp_network::http::ErrorCodeToString(
                  r.error().code())) +
        "] " + r.error().message();
    return false;
  }
  return true;
}

bool W4_InMemoryAnchorAccepted(std::string* detail) {
  Options options = ShortTimeouts();
  options.SetTls(Tls::Builder()
                     .SetCaPem(ReadFileOrDie(CertPath("ca_cert.pem")))
                     .Build());
  auto r = WebSocket::Connect(WsTlsBase(), options);
  if (!r.ok()) {
    *detail = r.error().message();
    return false;
  }
  return r.value().IsOpen();
}

bool W5_InvalidSchemeFastFail(std::string* /*detail*/) {
  auto bad = WebSocket::Connect("ftp://example.org/f", ShortTimeouts());
  if (bad.ok() || bad.error().code() != ErrorCode::kInvalidArgument) {
    return false;
  }
  auto empty = WebSocket::Connect("", ShortTimeouts());
  return !empty.ok() && empty.error().code() == ErrorCode::kInvalidArgument;
}

bool W6_BinaryRoundTripFragmentedTransparent(std::string* detail) {
  auto r = WebSocket::Connect(WsPlainBase(), ShortTimeouts());
  if (!r.ok()) {
    *detail = r.error().message();
    return false;
  }
  WebSocket ws = r.TakeValue();
  static constexpr size_t kPayloadSize = 8u * 1024 * 1024;
  WsMessage big;
  big.is_text = false;
  big.data.resize(kPayloadSize);
  for (size_t i = 0; i < kPayloadSize; ++i) {
    big.data[i] = static_cast<uint8_t>(i * 31 + (i >> 8));
  }
  if (!ws.Send(big).ok()) {
    *detail = "big send failed";
    return false;
  }
  auto got = ws.Receive();
  if (!got.ok()) {
    *detail = got.error().message();
    return false;
  }
  if (got.value().data != big.data) {
    *detail = "8MB round-trip mismatch";
    return false;
  }
  return true;
}

bool W7_MtlsPair(std::string* detail) {
  // Without client cert: rejected.
  Options no_cert = ShortTimeouts();
  no_cert.SetTls(Tls::Builder().SetVerifyMode(
      cpp_network::http::VerifyMode::kSkipVerification).Build());
  auto rejected = WebSocket::Connect(WsMtlsWsBase(), no_cert);
  if (rejected.ok() && rejected.value().IsOpen()) {
    *detail = "mTLS endpoint accepted connection without client cert";
    return false;
  }
  // With client material: accepted (server anchor via skip-verify isolate).
  Options with_cert = no_cert;
  with_cert.SetTls(Tls::Builder()
                       .SetVerifyMode(
                           cpp_network::http::VerifyMode::kSkipVerification)
                       .SetCertificate(CertPath("client_cert.pem"),
                                       CertPath("client_key.pem"))
                       .Build());
  auto accepted = WebSocket::Connect(WsMtlsWsBase(), with_cert);
  if (!accepted.ok()) {
    *detail = "with-cert connect failed: " + accepted.error().message();
    return false;
  }
  return accepted.value().IsOpen();
}

bool W8_PeerCloseDetails(std::string* detail) {
  auto r = WebSocket::Connect(WsPeerCloseBase(), ShortTimeouts());
  if (!r.ok()) {
    *detail = r.error().message();
    return false;
  }
  auto got = r.value().Receive();  // peer closes right after handshake
  if (got.ok()) {
    *detail = "expected close-carrying failure";
    return false;
  }
  if (got.error().code() != ErrorCode::kConnectionClosed) {
    *detail =
        "[" + std::string(cpp_network::http::ErrorCodeToString(
                  got.error().code())) +
        "] " + got.error().message();
    return false;
  }
  if (got.error().close_code() != 1000 ||
      got.error().close_reason() != "bye") {
    *detail = "close details mismatch";
    return false;
  }
  return true;
}

const Scenario kExternalScenarios[] = {
    {1, "HTTPS GET example.com (200 + body)", E1_SiteGet},
    {2, "HEAD + case-insensitive header read", E2_SiteHeaderRead},
    {3, "HTTPS POST JSON echo (httpbin)", E3_PostJsonEcho},
};

// Local-fixture mode (NETLIB_TEST_MODE=local): the seven certificate-oriented
// scenarios S1-S7 require reachable self-signed/mTLS services. They remain
// valuable on hosts where src/tests fixtures run natively; Android gateways
// without a shared network segment cannot reach them (see research.md).
bool S1_DefaultRejectsSelfSigned(std::string* detail) {
  auto client = Client::Create(ShortTimeouts());
  if (!client.ok()) {
    *detail = client.error().message();
    return false;
  }
  Result<Response> res = client->Get(TlsBase() + "/");
  if (res.ok()) {
    *detail = "unexpectedly succeeded against self-signed server";
    return false;
  }
  if (res.error().code() != ErrorCode::kCertificateVerificationFailed) {
    *detail = "expected kCertificateVerificationFailed, got [" +
              std::string(cpp_network::http::ErrorCodeToString(
                  res.error().code())) +
              "] " + res.error().message();
    return false;
  }
  return true;
}

bool S2_CaFileInjected(std::string* detail) {
  Options options = ShortTimeouts();
  options.SetTls(Tls::Builder().SetCaFile(CertPath("ca_cert.pem")).Build());
  auto client = Client::Create(options);
  if (!client.ok()) {
    *detail = client.error().message();
    return false;
  }
  Result<Response> res = client->Get(TlsBase() + "/");
  if (!res.ok()) {
    *detail = res.error().message();
    return false;
  }
  if (res->status() != 200) {
    *detail = "status " + std::to_string(res->status());
    return false;
  }
  return true;
}

bool S3_CaPemInjected(std::string* detail) {
  std::string pem = ReadFileOrDie(CertPath("ca_cert.pem"));
  Options options = ShortTimeouts();
  options.SetTls(Tls::Builder().SetCaPem(pem).Build());
  auto client = Client::Create(options);
  if (!client.ok()) {
    *detail = client.error().message();
    return false;
  }
  Result<Response> res = client->Get(TlsBase() + "/");
  if (!res.ok()) {
    *detail = res.error().message();
    return false;
  }
  if (res->status() != 200) {
    *detail = "status " + std::to_string(res->status());
    return false;
  }
  return true;
}

bool S4_MtlsRejectedWithoutClientCert(std::string* detail) {
  Options options = ShortTimeouts();
  options.SetTls(Tls::Builder()
                     .SetVerifyMode(cpp_network::http::VerifyMode::
                                        kSkipVerification)
                     .Build());  // skip SERVER validation: isolates client-cert gate
  auto client = Client::Create(options);
  if (!client.ok()) {
    *detail = client.error().message();
    return false;
  }
  Result<Response> res = client->Get(MtlsBase() + "/");
  if (res.ok() && res->status() == 200) {
    *detail = "server accepted request without client certificate";
    return false;
  }
  // Server-driven rejection may surface as any transport/handshake error.
  return true;
}

bool S5_MtlsWithClientCertPath(std::string* detail) {
  Options options = ShortTimeouts();
  options.SetTls(Tls::Builder()
                     .SetVerifyMode(
                         cpp_network::http::VerifyMode::kSkipVerification)
                     .SetCertificate(CertPath("client_cert.pem"),
                                     CertPath("client_key.pem"))
                     .Build());
  auto client = Client::Create(options);
  if (!client.ok()) {
    *detail = client.error().message();
    return false;
  }
  Result<Response> res = client->Get(MtlsBase() + "/");
  if (!res.ok()) {
    *detail = res.error().message();
    return false;
  }
  if (res->status() != 200) {
    *detail = "status " + std::to_string(res->status());
    return false;
  }
  return true;
}

bool S6_SkipVerificationAccepted(std::string* detail) {
  Options options = ShortTimeouts();
  options.SetTls(
      Tls::Builder().SetVerifyMode(
          cpp_network::http::VerifyMode::kSkipVerification).Build());
  auto client = Client::Create(options);
  if (!client.ok()) {
    *detail = client.error().message();
    return false;
  }
  Result<Response> res = client->Head(TlsBase() + "/");
  if (!res.ok()) {
    *detail = res.error().message();
    return false;
  }
  return true;
}

bool S7_HttpBaseline404Header(std::string* detail) {
  auto client = Client::Create(ShortTimeouts());
  if (!client.ok()) {
    *detail = client.error().message();
    return false;
  }
  Result<Response> res = client->Get(HttpBase() + "/notfound");
  if (!res.ok()) {
    *detail = res.error().message();
    return false;
  }
  if (res->status() != 404) {
    *detail = "status " + std::to_string(res->status());
    return false;
  }
  auto header = res->GetHeader("X-Custom");
  if (!header.has_value() || *header != "value") {
    *detail = "missing X-Custom header";
    return false;
  }
  return true;
}

const Scenario kWssExternalScenarios[] = {
    {1, "wss public echo (text roundtrip)", W1_WssPublicEcho},
};

const Scenario kWssLocalScenarios[] = {
    {1, "plaintext ws connect opens", W2_PlainConnectOpens},
    {2, "self-signed wss rejected by default", W3_SelfSignedWssRejectedByDefault},
    {3, "in-memory CA anchor accepted", W4_InMemoryAnchorAccepted},
    {4, "invalid scheme fast fail", W5_InvalidSchemeFastFail},
    {5, "8MB binary echo (fragmentation transparent)", W6_BinaryRoundTripFragmentedTransparent},
    {6, "mTLS pair", W7_MtlsPair},
    {7, "peer close details via Receive", W8_PeerCloseDetails},
};

const Scenario kLocalScenarios[] = {
    {1, "self-signed rejected by default", S1_DefaultRejectsSelfSigned},
    {2, "CA file injection accepted", S2_CaFileInjected},
    {3, "in-memory CA PEM accepted", S3_CaPemInjected},
    {4, "mTLS rejected without client cert", S4_MtlsRejectedWithoutClientCert},
    {5, "mTLS accepted with client cert files", S5_MtlsWithClientCertPath},
    {6, "skip verification accepted", S6_SkipVerificationAccepted},
    {7, "HTTP baseline 404 + header read", S7_HttpBaseline404Header},
};

}  // namespace

int main() {
  const bool local_mode =
      std::getenv("NETLIB_TEST_MODE") != nullptr &&
      std::string(std::getenv("NETLIB_TEST_MODE")) == "local";
  const Scenario* scenarios = local_mode ? kLocalScenarios : kExternalScenarios;
  const int base_total =
      static_cast<int>(local_mode ? sizeof(kLocalScenarios)
                                  : sizeof(kExternalScenarios)) /
      sizeof(Scenario);
  // WebSocket block appended after the protocol baseline set (specs/006);
  // ids restart at 1 but the printed tag keeps the streams distinguishable.
  const Scenario* ws_scenarios =
      local_mode ? kWssLocalScenarios : kWssExternalScenarios;
  const int ws_total =
      static_cast<int>(local_mode ? sizeof(kWssLocalScenarios)
                                  : sizeof(kWssExternalScenarios)) /
      sizeof(Scenario);

  int passed = 0;
  int grand_total = base_total + ws_total;
  bool any_failure = false;
  for (int pass = 0; pass < 2; ++pass) {
    const char* tag = pass == 0 ? (local_mode ? "S" : "E") : "W";
    const Scenario* list = pass == 0 ? scenarios : ws_scenarios;
    const int count = pass == 0 ? base_total : ws_total;
    for (int i = 0; i < count; ++i) {
      const Scenario& scenario = list[i];
      std::string detail;
      const bool ok = scenario.run(&detail);
      std::printf("[%s%d] %s : %s%s%s\n", tag, scenario.id,
                  ok ? "PASS" : "FAIL", scenario.name,
                  ok ? "" : " -- ", ok ? "" : detail.c_str());
      std::fflush(stdout);
      if (ok) {
        ++passed;
      } else {
        any_failure = true;
      }
    }
  }

  std::printf("PASS %d/%d\n", passed, grand_total);
  std::fflush(stdout);
  return any_failure ? 95 : 0;
}
