#include "netlib/netlib.h"

#include "gtest/gtest.h"

namespace netlib {

// Smoke test verifying the engineering skeleton is buildable and testable.
TEST(SmokeTest, UmbrellaHeaderCompiles) {
  // NETLIB_API is defined by netlib_export.h (included via netlib.h).
  EXPECT_NE(nullptr, "netlib umbrella header compiles");
}

}  // namespace netlib
