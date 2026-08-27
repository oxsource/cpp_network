#include "http/http_umbrella.h"

#include <cstdio>

int main() {
  // The cpp_network umbrella header compiles and links when consumed as an external
  // dependency (@cpp_network//:cpp_network). CPP_NETWORK_HTTP_EXPORT is defined by
  // http/export.h (included via http_umbrella.h).
  const char* message = "consumer_demo: cpp_network consumed successfully";
  std::puts(message);
  std::printf("error code sample: %s\n",
              cpp_network::http::ErrorCodeToString(
                  cpp_network::http::ErrorCode::kConnectionTimeout));
  return 0;
}
