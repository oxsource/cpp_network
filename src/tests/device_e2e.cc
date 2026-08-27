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
#include <vector>

#include "test_util.h"

using cpp_network::http::CertPath;
using cpp_network::http::Client;
using cpp_network::http::ErrorCode;
using cpp_network::http::Error;
using cpp_network::http::HttpBase;
using cpp_network::http::MtlsBase;
using cpp_network::http::Options;
using cpp_network::http::Result;
using cpp_network::http::Response;
using cpp_network::http::Tls;
using cpp_network::http::TlsBase;

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
  options.SetTotalTimeout(std::chrono::milliseconds(10000));
  return options;
}

struct Scenario {
  int id;
  const char* name;
  bool (*run)(std::string* detail);
};

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

const Scenario kScenarios[] = {
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
  const int total = sizeof(kScenarios) / sizeof(kScenarios[0]);
  int passed = 0;
  int first_failure = 0;
  for (const Scenario& scenario : kScenarios) {
    std::string detail;
    const bool ok = scenario.run(&detail);
    std::printf("[S%d] %s : %s%s%s\n", scenario.id,
                ok ? "PASS" : "FAIL", scenario.name,
                ok ? "" : " -- ", ok ? "" : detail.c_str());
    std::fflush(stdout);
    if (ok) {
      ++passed;
      continue;
    }
    if (first_failure == 0) first_failure = scenario.id;
  }

  std::printf("PASS %d/%d\n", passed, total);
  std::fflush(stdout);
  return first_failure == 0 ? 0 : first_failure + 1;
}
