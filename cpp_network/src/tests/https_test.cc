#include "http/http_umbrella.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

#include "test_util.h"
#include "gtest/gtest.h"

namespace cpp_network {
namespace http {

// Shared core types (canonical home: cpp_network::comm).
using cpp_network::comm::ErrorCode;
using cpp_network::comm::Options;
using cpp_network::comm::Result;
using cpp_network::comm::Tls;
using cpp_network::comm::VerifyMode;


namespace {

constexpr int kTlsPort = 18443;
const std::string kTlsBase = "https://127.0.0.1:18443";
pid_t g_tls_pid = -1;
pid_t g_mtls_pid = -1;

Options MakeOptions() {
  Options opts;
  opts.SetConnectTimeout(std::chrono::seconds(5))
      .SetReadTimeout(std::chrono::seconds(5));
  return opts;
}

bool ReadFile(const std::string& path, std::string* out) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) return false;
  std::ostringstream buffer;
  buffer << file.rdbuf();
  *out = buffer.str();
  return true;
}

pid_t StartServer(const std::string& script, int port,
                  const std::string& extra_arg = "") {
  pid_t pid = fork();
  if (pid == 0) {
    // Child: exec python3 TLS server.
    if (!extra_arg.empty()) {
      execlp("python3", "python3", script.c_str(), "--port",
             std::to_string(port).c_str(), extra_arg.c_str(),
             (char*)nullptr);
    } else {
      execlp("python3", "python3", script.c_str(), "--port",
             std::to_string(port).c_str(), (char*)nullptr);
    }
    _exit(127);
  }
  return pid;
}

bool WaitUntilReady(const std::string& url, int attempts = 100,
                    bool mtls = false) {
  for (int i = 0; i < attempts; ++i) {
    std::string cmd = "curl -sk -o /dev/null " + url + " >/dev/null 2>&1";
    if (mtls) {
      cmd = "curl -sk --cert src/tests/certs/client_cert.pem "
            "--key src/tests/certs/client_key.pem -o /dev/null " + url +
            " >/dev/null 2>&1";
    }
    if (system(cmd.c_str()) == 0) return true;
    usleep(100000);
  }
  return false;
}

}  // namespace

class HttpsTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    g_tls_pid = StartServer("src/tests/test_tls_server.py", kTlsPort);
    g_mtls_pid = StartServer("src/tests/test_tls_server.py", kTlsPort + 1,
                             "--require-client-cert");
    ASSERT_TRUE(WaitUntilReady(kTlsBase));
    ASSERT_TRUE(WaitUntilReady("https://127.0.0.1:18444", 100, /*mtls=*/true));
  }
  static void TearDownTestSuite() {
    if (g_tls_pid > 0) kill(g_tls_pid, SIGTERM);
    if (g_mtls_pid > 0) kill(g_mtls_pid, SIGTERM);
  }

  void SetUp() override {
    auto client = Client::Create(MakeOptions());
    ASSERT_TRUE(client.ok()) << client.error().message();
    client_ = std::make_unique<Client>(std::move(*client));
  }

  std::unique_ptr<Client> client_;
};

TEST_F(HttpsTest, SelfSignedRejectedByDefault) {
  Result<Response> res = client_->Get(kTlsBase + "/");
  ASSERT_FALSE(res.ok());
  EXPECT_EQ(ErrorCode::kCertificateVerificationFailed, res.error().code());
}

TEST_F(HttpsTest, SelfSignedAcceptedWhenSkipVerification) {
  Options opts = MakeOptions();
  opts.SetTls(Tls::Builder().SetVerifyMode(VerifyMode::kSkipVerification)
                  .Build());
  auto client = Client::Create(opts);
  ASSERT_TRUE(client.ok()) << client.error().message();
  Result<Response> res = client->Get(kTlsBase + "/");
  ASSERT_TRUE(res.ok()) << res.error().message();
  EXPECT_EQ(200, res->status());
}

TEST_F(HttpsTest, SelfSignedAcceptedWhenCaFileInjected) {
  Options opts = MakeOptions();
  opts.SetTls(Tls::Builder().SetCaFile(CertPath("ca_cert.pem")).Build());
  auto client = Client::Create(opts);
  ASSERT_TRUE(client.ok()) << client.error().message();
  Result<Response> res = client->Get(kTlsBase + "/");
  ASSERT_TRUE(res.ok()) << res.error().message();
  EXPECT_EQ(200, res->status());
}

TEST_F(HttpsTest, SelfSignedAcceptedWhenCaPemInjected) {
  std::string ca_pem;
  ASSERT_TRUE(ReadFile(CertPath("ca_cert.pem"), &ca_pem));
  Options opts = MakeOptions();
  opts.SetTls(Tls::Builder().SetCaPem(ca_pem).Build());
  auto client = Client::Create(opts);
  ASSERT_TRUE(client.ok()) << client.error().message();
  Result<Response> res = client->Get(kTlsBase + "/");
  ASSERT_TRUE(res.ok()) << res.error().message();
  EXPECT_EQ(200, res->status());
}

TEST_F(HttpsTest, ClientCertificateRequiredForMtls) {
  Options opts = MakeOptions();
  opts.SetTls(
      Tls::Builder()
          .SetVerifyMode(VerifyMode::kSkipVerification)
          .SetCertificate(CertPath("client_cert.pem"),
                          CertPath("client_key.pem"))
          .Build());
  auto client = Client::Create(opts);
  ASSERT_TRUE(client.ok()) << client.error().message();
  Result<Response> res = client->Get("https://127.0.0.1:18444/");
  ASSERT_TRUE(res.ok()) << res.error().message();
  EXPECT_EQ(200, res->status());
}

TEST_F(HttpsTest, MtlsAcceptedWithInMemoryPem) {
  std::string cert_pem;
  std::string key_pem;
  ASSERT_TRUE(ReadFile(CertPath("client_cert.pem"), &cert_pem));
  ASSERT_TRUE(ReadFile(CertPath("client_key.pem"), &key_pem));
  Options opts = MakeOptions();
  opts.SetTls(Tls::Builder()
                  .SetVerifyMode(VerifyMode::kSkipVerification)
                  .SetCertificate(cert_pem, key_pem)
                  .Build());
  auto client = Client::Create(opts);
  ASSERT_TRUE(client.ok()) << client.error().message();
  Result<Response> res = client->Get("https://127.0.0.1:18444/");
  ASSERT_TRUE(res.ok()) << res.error().message();
  EXPECT_EQ(200, res->status());
}

// Validation tests exercise Tls::Validate() via Client::Create; no server
// interaction is needed since invalid configurations are rejected up front.

TEST(TlsValidationTest, InvalidInlineCaPemRejected) {
  Options opts;
  opts.SetTls(Tls::Builder().SetCaPem("this is not a pem").Build());
  auto client = Client::Create(opts);
  ASSERT_FALSE(client.ok());
  EXPECT_EQ(ErrorCode::kInvalidArgument, client.error().code());
}

TEST(TlsValidationTest, ConflictingCaSourcesRejected) {
  Options opts;
  opts.SetTls(Tls::Builder()
                  .SetCaFile(CertPath("ca_cert.pem"))
                  .SetCaPem("-----BEGIN CERTIFICATE-----\n"
                            "-----END CERTIFICATE-----")
                  .Build());
  auto client = Client::Create(opts);
  ASSERT_FALSE(client.ok());
  EXPECT_EQ(ErrorCode::kInvalidArgument, client.error().code());
}

TEST(TlsValidationTest, SniWithCrlfRejected) {
  Options opts;
  opts.SetTls(Tls::Builder().SetSni("example.com\r\nX-Injected: 1").Build());
  auto client = Client::Create(opts);
  ASSERT_FALSE(client.ok());
  EXPECT_EQ(ErrorCode::kInvalidArgument, client.error().code());
}

TEST(TlsValidationTest, MixedPemAndPathClientMaterialRejected) {
  Options opts;
  opts.SetTls(Tls::Builder()
                  .SetCertificate(
                      CertPath("client_cert.pem"),
                      "-----BEGIN PRIVATE KEY-----\n-----END PRIVATE KEY-----")
                  .Build());
  auto client = Client::Create(opts);
  ASSERT_FALSE(client.ok());
  EXPECT_EQ(ErrorCode::kInvalidArgument, client.error().code());
}

}  // namespace http

}  // namespace cpp_network
