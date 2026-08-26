#include "netlib/netlib.h"

#include <cstdio>

int main() {
  // The netlib umbrella header compiles and links when consumed as an external
  // dependency (@cpp_network//:netlib). NETLIB_API is defined by
  // netlib_export.h (included via netlib.h).
  const char* message = "consumer_demo: netlib consumed successfully";
  std::puts(message);
  return 0;
}
