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

namespace {

constexpr int kPort = 18080;
const std::string kBase = "http://127.0.0.1:18080";
pid_t g_server_pid = -1;

void StartServer() {
  g_server_pid = fork();
  if (g_server_pid == 0) {
    execlp("python3", "python3", "src/tests/test_server.py", "--port",
           std::to_string(kPort).c_str(), (char*)nullptr);
    _exit(127);
  }
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

class ConfigTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() { StartServer(); }
  static void TearDownTestSuite() { StopServer(); }
};

TEST_F(ConfigTest, ConnectTimeoutOnUnreachableAddress) {
  Options opts;
  opts.SetConnectTimeout(std::chrono::milliseconds(500))
      .SetReadTimeout(std::chrono::seconds(2));
  auto client = Client::Create(opts);
  ASSERT_TRUE(client.ok()) << client.error().message();

  // 10.255.255.1 is a non-routable address; connection should time out.
  Result<Response> res = client->Get("http://10.255.255.1:81/");
  ASSERT_FALSE(res.ok());
  EXPECT_EQ(ErrorCode::kConnectionTimeout, res.error().code());
}

TEST_F(ConfigTest, ReadTimeoutOnSlowServer) {
  Options opts;
  opts.SetConnectTimeout(std::chrono::seconds(2))
      .SetReadTimeout(std::chrono::seconds(1))
      .SetTotalTimeout(std::chrono::seconds(4));
  auto client = Client::Create(opts);
  ASSERT_TRUE(client.ok()) << client.error().message();

  Result<Response> res = client->Get(kBase + "/slow");
  ASSERT_FALSE(res.ok());
  EXPECT_EQ(ErrorCode::kTotalTimeout, res.error().code());
}

TEST_F(ConfigTest, RedirectFollowedByDefault) {
  Options opts;
  opts.SetConnectTimeout(std::chrono::seconds(5))
      .SetReadTimeout(std::chrono::seconds(5))
      .SetFollowRedirects(true);
  auto client = Client::Create(opts);
  ASSERT_TRUE(client.ok()) << client.error().message();

  Result<Response> res = client->Get(kBase + "/redirect");
  ASSERT_TRUE(res.ok()) << res.error().message();
  EXPECT_EQ(200, res->status());
  EXPECT_EQ("redirected ok", res->body());
  EXPECT_NE(res->effective_url().find("/redirected"), std::string::npos);
}

TEST_F(ConfigTest, RedirectNotFollowedWhenDisabled) {
  Options opts;
  opts.SetConnectTimeout(std::chrono::seconds(5))
      .SetReadTimeout(std::chrono::seconds(5))
      .SetFollowRedirects(false);
  auto client = Client::Create(opts);
  ASSERT_TRUE(client.ok()) << client.error().message();

  Result<Response> res = client->Get(kBase + "/redirect");
  ASSERT_TRUE(res.ok()) << res.error().message();
  EXPECT_EQ(302, res->status());
}

}  // namespace http
}  // namespace cpp_network
