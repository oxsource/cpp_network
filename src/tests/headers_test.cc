#include "http/headers.h"

#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace cpp_network {
namespace http {

TEST(HeadersTest, EmptyByDefault) {
  Headers headers;
  EXPECT_TRUE(headers.empty());
  EXPECT_EQ(0, headers.size());
  EXPECT_FALSE(headers.Has("anything"));
  EXPECT_FALSE(headers.Get("anything").has_value());
  EXPECT_TRUE(headers.GetAll("anything").empty());
}

TEST(HeadersTest, AddPreservesOrderAndDuplicates) {
  Headers headers =
      Headers::Builder()
          .Add("Set-Cookie", "a=1")
          .Add("Content-Type", "text/plain")
          .Add("Set-Cookie", "b=2")
          .Build();
  ASSERT_EQ(3, headers.size());
  EXPECT_EQ("Set-Cookie", headers.name(0));
  EXPECT_EQ("a=1", headers.value(0));
  EXPECT_EQ("Content-Type", headers.name(1));
  EXPECT_EQ("b=2", headers.value(2));

  std::vector<std::string> cookies = headers.GetAll("set-cookie");
  ASSERT_EQ(2u, cookies.size());
  EXPECT_EQ("a=1", cookies[0]);
  EXPECT_EQ("b=2", cookies[1]);
}

TEST(HeadersTest, GetIsCaseInsensitiveFirstMatch) {
  Headers headers =
      Headers::Builder()
          .Add("x-Lower", "first")
          .Add("X-LOWER", "second")
          .Build();
  auto value = headers.Get("X-lower");
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ("first", *value);
  EXPECT_EQ(2u, headers.GetAll("x-lower").size());
}

TEST(HeadersTest, SetReplacesAllOccurrencesCaseInsensitively) {
  Headers headers = Headers::Builder()
                        .Add("Accept", "*/*")
                        .Add("content-type", "text/plain")
                        .Add("Content-Type", "text/html")
                        .Set("CONTENT-TYPE", "application/json")
                        .Build();
  ASSERT_EQ(2, headers.size());
  EXPECT_EQ("*/*", headers.value(0));
  EXPECT_EQ("application/json", headers.Get("content-type"));
}

TEST(HeadersTest, RemoveAndClear) {
  Headers::Builder builder;
  builder.Add("A", "1").Add("b", "2").Remove("B");
  EXPECT_FALSE(builder.Has("b"));
  builder.Add("a", "9").Clear();
  EXPECT_EQ(0, builder.fields().size());

  Headers headers = builder.Add("X", "1").Build();
  EXPECT_FALSE(headers.Has("a"));
  EXPECT_TRUE(headers.Has("x"));
}

TEST(HeadersTest, BuilderGetLooksAtAccumulatedFieldsOnly) {
  Headers::Builder builder;
  builder.Add("Etag", "\"v1\"");
  EXPECT_TRUE(builder.Has("ETAG"));
  EXPECT_FALSE(builder.Has("If-None-Match"));
  builder.Build();  // Build does not mutate the builder.
  EXPECT_TRUE(builder.Has("etag"));
}

TEST(HeadersTest, EqualityIsFieldLineWise) {
  Headers a = Headers::Builder().Add("A", "1").Add("B", "2").Build();
  Headers b = Headers::Builder().Add("a", "1").Add("b", "2").Build();
  Headers c = Headers::Builder().Add("B", "2").Add("A", "1").Build();
  // Name case is irrelevant; ordering and values matter.
  EXPECT_EQ(a, b);
  EXPECT_NE(a, c);
}

}  // namespace http
}  // namespace cpp_network
