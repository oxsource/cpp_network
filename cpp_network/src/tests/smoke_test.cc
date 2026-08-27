#include "http/http_umbrella.h"

#include "gtest/gtest.h"

namespace cpp_network {
namespace http {

// Smoke test verifying the engineering skeleton is buildable and testable.
TEST(SmokeTest, UmbrellaHeaderCompiles) {
  // cpp_network::http umbrella header (http.h) compiles and exposes types.
  EXPECT_NE(nullptr, "http umbrella header compiles");
}

}  // namespace http
}  // namespace cpp_network
