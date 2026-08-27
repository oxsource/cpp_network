#include "http/http_umbrella.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

#include "gtest/gtest.h"

namespace cpp_network {
namespace http {

// Shared core types (canonical home: cpp_network::comm).
using cpp_network::comm::ErrorCode;
using cpp_network::comm::Options;
using cpp_network::comm::Result;
using cpp_network::comm::Url;


namespace {

constexpr int kTestPort = 18080;
const std::string kBase = "http://127.0.0.1:18080";
pid_t g_server_pid = -1;

Options MakeOptions() {
  Options opts;
  opts.SetConnectTimeout(std::chrono::seconds(5))
      .SetReadTimeout(std::chrono::seconds(5))
      .SetFollowRedirects(true);
  return opts;
}

void StartServer() {
  // Locate the test server script relative to the runfiles tree.
  std::string script = "src/tests/test_server.py";
  g_server_pid = fork();
  if (g_server_pid == 0) {
    // Child: exec python3 test server.
    execlp("python3", "python3", script.c_str(), "--port",
           std::to_string(kTestPort).c_str(), (char*)nullptr);
    _exit(127);
  }
  // Wait for server to be ready.
  for (int i = 0; i < 100; ++i) {
    std::string cmd = "curl -s -o /dev/null " + kBase + "/ >/dev/null 2>&1";
    if (system(cmd.c_str()) == 0) return;
    usleep(100000);
  }
  FAIL() << "test server did not start";
}

void StopServer() {
  if (g_server_pid > 0) {
    kill(g_server_pid, SIGTERM);
    waitpid(g_server_pid, nullptr, 0);
    g_server_pid = -1;
  }
}

}  // namespace

class HttpIntegrationTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() { StartServer(); }
  static void TearDownTestSuite() { StopServer(); }

  void SetUp() override {
    auto client = Client::Create(MakeOptions());
    ASSERT_TRUE(client.ok()) << client.error().message();
    client_ = std::make_unique<Client>(std::move(*client));
  }

  std::unique_ptr<Client> client_;
};

TEST_F(HttpIntegrationTest, Get200ReturnsBody) {
  Result<Response> res = client_->Get(kBase + "/");
  ASSERT_TRUE(res.ok()) << res.error().message();
  EXPECT_EQ(200, res->status());
  EXPECT_TRUE(res->ok());
  EXPECT_EQ("Hello World", res->body());
}

TEST_F(HttpIntegrationTest, Get404ReturnsStatusAndHeader) {
  Result<Response> res = client_->Get(kBase + "/notfound");
  ASSERT_TRUE(res.ok()) << res.error().message();
  EXPECT_EQ(404, res->status());
  EXPECT_FALSE(res->ok());
  auto header = res->GetHeader("X-Custom");
  ASSERT_TRUE(header.has_value());
  EXPECT_EQ("value", *header);
}

TEST_F(HttpIntegrationTest, PostJsonBody) {
  Request req = Request::Builder()
                    .SetMethod(Method::kPost)
                    .Url(kBase + "/echo_json")
                    .JsonBody(R"({"name":"cpp_network"})")
                    .Build()
                    .value();
  Result<Response> res = client_->Post(req);
  ASSERT_TRUE(res.ok()) << res.error().message();
  EXPECT_EQ(200, res->status());
  EXPECT_NE(res->body().find("cpp_network"), std::string::npos);
}

TEST_F(HttpIntegrationTest, PostEcho) {
  Result<Response> res = client_->Post(kBase + "/echo", "payload123");
  ASSERT_TRUE(res.ok()) << res.error().message();
  EXPECT_EQ(200, res->status());
  EXPECT_EQ("payload123", res->body());
}

TEST_F(HttpIntegrationTest, DuplicateHeadersPreservedAndCaseInsensitive) {
  Result<Response> res = client_->Get(kBase + "/duplicates");
  ASSERT_TRUE(res.ok()) << res.error().message();
  const Headers& headers = res->headers();
  // Duplicate field lines are preserved in order (Set-Cookie style).
  EXPECT_EQ(2, headers.GetAll("set-cookie").size());
  std::vector<std::string> cookies = headers.GetAll("SET-COOKIE");
  ASSERT_EQ(2u, cookies.size());
  EXPECT_EQ("a=1; Path=/", cookies[0]);
  EXPECT_EQ("b=2; Path=/", cookies[1]);
  // Lookup is case-insensitive; first match wins.
  auto first = headers.Get("x-CASE-test");
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ("vAlUe", *first);
  EXPECT_TRUE(headers.Has("X-CASE-TEST"));
  // Indexed access covers every field line.
  int content_type_lines = 0;
  for (int i = 0; i < headers.size(); ++i) {
    if (headers.name(i) == "Content-Type") ++content_type_lines;
  }
  EXPECT_EQ(1, content_type_lines);
}

TEST_F(HttpIntegrationTest, InvalidUrlRejected) {
  Result<Request> build =
      Request::Builder().Url("not-a-url").Build();
  ASSERT_FALSE(build.ok());
  EXPECT_EQ(ErrorCode::kInvalidArgument, build.error().code());
}

}  // namespace http
}  // namespace cpp_network
