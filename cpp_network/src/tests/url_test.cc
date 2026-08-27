#include "http/http_umbrella.h"

#include "gtest/gtest.h"

namespace cpp_network {
namespace http {

// Shared core types (canonical home: cpp_network::comm).
using cpp_network::comm::ErrorCode;
using cpp_network::comm::Url;


TEST(UrlTest, ParseFullUrl) {
  auto url = Url::Parse(
      "https://api.example.com:8443/v1/users/42?sort=name&page=2#top");
  ASSERT_TRUE(url.ok()) << url.error().message();
  EXPECT_EQ("https", url->scheme());
  EXPECT_EQ("api.example.com", url->host());
  ASSERT_TRUE(url->explicit_port().has_value());
  EXPECT_EQ(8443, *url->explicit_port());
  EXPECT_EQ(8443, url->port());
  EXPECT_EQ((std::vector<std::string>{"v1", "users", "42"}),
            url->path_segments());
  ASSERT_EQ(2, url->query_parameters().size());
  EXPECT_EQ(std::make_pair(std::string("sort"), std::string("name")),
            url->query_parameters()[0]);
  EXPECT_EQ(std::make_pair(std::string("page"), std::string("2")),
            url->query_parameters()[1]);
  EXPECT_EQ("top", url->fragment());
}

TEST(UrlTest, DefaultPorts) {
  auto http_url = Url::Parse("http://example.com/");
  ASSERT_TRUE(http_url.ok()) << http_url.error().message();
  EXPECT_FALSE(http_url->explicit_port().has_value());
  EXPECT_EQ(80, http_url->port());

  auto https_url = Url::Parse("https://example.com");
  ASSERT_TRUE(https_url.ok()) << https_url.error().message();
  EXPECT_EQ(443, https_url->port());
}

TEST(UrlTest, RoundTripToString) {
  const std::string raw = "https://example.com/a/b?k=v&x=1";
  auto url = Url::Parse(raw);
  ASSERT_TRUE(url.ok()) << url.error().message();
  // The default port is omitted on re-serialization.
  EXPECT_EQ("https://example.com/a/b?k=v&x=1", url->ToString());
}

TEST(UrlTest, BareAuthorityGetsRootPath) {
  auto url = Url::Parse("https://example.com");
  ASSERT_TRUE(url.ok()) << url.error().message();
  EXPECT_TRUE(url->path_segments().empty());
  EXPECT_EQ("https://example.com/", url->ToString());
}

TEST(UrlTest, PercentEncodingRoundTrip) {
  Url::Builder builder;
  builder.SetScheme("https")
      .SetHost("example.com")
      .AddPathSegment("a b")
      .AddPathSegment("c/d")
      .AddQueryParameter("q", "a&b=c")
      .SetFragment("frag ment");
  auto built = builder.Build();
  ASSERT_TRUE(built.ok()) << built.error().message();
  const std::string rendered = built->ToString();
  // Reserved characters are percent-encoded per component.
  EXPECT_EQ("https://example.com/a%20b/c%2Fd?q=a%26b%3Dc#frag%20ment",
            rendered);

  // Decoded accessors return the original values.
  EXPECT_EQ((std::vector<std::string>{"a b", "c/d"}), built->path_segments());
  ASSERT_EQ(1, built->query_parameters().size());
  EXPECT_EQ("a&b=c", built->query_parameters()[0].second);
}

TEST(UrlTest, DecodePercentEscapesOnParse) {
  auto url = Url::Parse("https://example.com/my%20file.txt?q=a+b");
  ASSERT_TRUE(url.ok()) << url.error().message();
  EXPECT_EQ("my file.txt", url->path_segments()[0]);
  // '+' means space only in the query component.
  ASSERT_EQ(1, url->query_parameters().size());
  EXPECT_EQ("a b", url->query_parameters()[0].second);
}

TEST(UrlTest, SchemeAndHostCaseNormalized) {
  auto url = Url::Parse("HTTP://EXAMPLE.COM/Path");
  ASSERT_TRUE(url.ok()) << url.error().message();
  EXPECT_EQ("http", url->scheme());
  EXPECT_EQ("example.com", url->host());
  EXPECT_EQ("Path", url->path_segments()[0]);  // path case is preserved
}

TEST(UrlTest, Ipv6Literal) {
  auto url = Url::Parse("http://[::1]:8080/x");
  ASSERT_TRUE(url.ok()) << url.error().message();
  EXPECT_EQ("[::1]", url->host());
  EXPECT_EQ(8080, url->port());
}

TEST(UrlTest, BuilderComposition) {
  auto base = Url::Parse("https://api.example.com/v1");
  ASSERT_TRUE(base.ok()) << base.error().message();
  auto url =
      Url::Builder::FromUrl(*base)
          .AddPathSegment("users")
          .AddQueryParameter("id", "42")
          .Build();
  ASSERT_TRUE(url.ok()) << url.error().message();
  EXPECT_EQ("https://api.example.com/v1/users?id=42", url->ToString());

  auto req = Request::Builder().Url(*url).Build();
  ASSERT_TRUE(req.ok()) << req.error().message();
  EXPECT_EQ("https://api.example.com/v1/users?id=42", req->url());
}

TEST(UrlTest, RejectsInvalidInput) {
  struct Case {
    const char* url;
    const char* why;
  };
  const Case cases[] = {
      {"ftp://example.com/", "non-http scheme"},
      {"example.com/path", "missing scheme"},
      {"https:///path", "missing host"},
      {"https://user:pass@example.com/", "userinfo rejected"},
      {"https://example.com:0/", "port below range"},
      {"https://example.com:99999/", "port above range"},
      {"https://example.com:abc/", "non-numeric port"},
      {"https://example.com/%zz", "invalid escape"},
  };
  for (const Case& c : cases) {
    auto res = Url::Parse(c.url);
    EXPECT_FALSE(res.ok()) << c.why << ": " << c.url;
    EXPECT_EQ(ErrorCode::kInvalidArgument, res.error().code()) << c.why;
  }
}

}  // namespace http
}  // namespace cpp_network
